#include "MainWindow.h"
#include "PlayerView.h"
#include "Timeline.h"
#include "Transport.h"
#include "Panels.h"
#include "Mixer.h"
#include "Export.h"
#include "Effects.h"
#include "Peaks.h"
#include "Tools.h"
#include "VoiceRecorder.h"
#include "SettingsDialog.h"
#include "ChannelFx.h"
#include "ScreenRecorder.h"
#include "Branding.h"
#include "AudioEngine.h"
#include <QDockWidget>
#include <QTabWidget>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QCloseEvent>
#include <QAction>
#include <QDateTime>
#include <functional>

MainWindow::MainWindow() {
    setWindowTitle("Ultimate Video Editor");
    resize(1600, 950);
    cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cacheDir);

    player = new PlayerView(&project);
    transport = new TransportBar;
    // плеер + кнопки — центральный виджет, все остальные окна — панели вокруг него (их можно двигать, отсоединять, ставить вкладками)
    QWidget* central = new QWidget;
    QVBoxLayout* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0); cl->setSpacing(0);
    cl->addWidget(player, 1);
    cl->addWidget(transport);
    central->setMinimumSize(420, 300);
    setCentralWidget(central);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    setDockNestingEnabled(true);

    timeline = new TimelineWidget(&project);
    inspector = new Inspector;
    mixer = new MixerPanel(&project);
    assets = new AssetsPanel;
    fxTree = new EffectsTree;
    anims = new AnimationsList;
    recorder = new VoiceRecorder(this);

    assetTabs = new QTabWidget;
    assetTabs->addTab(assets, "Медиа");
    assetTabs->addTab(anims, "Анимации");
    assetTabs->addTab(fxTree, "Шейдеры");
    dockAssets = addDock("Ассеты", "dock_assets", assetTabs, Qt::LeftDockWidgetArea);
    dInsp = addDock("Инспектор / ключевые кадры", "dock_inspector", inspector, Qt::RightDockWidgetArea);
    dMix = addDock("Звуковой микшер", "dock_mixer", mixer, Qt::RightDockWidgetArea);
    dTl = addDock("Таймлайн", "dock_timeline", timeline, Qt::BottomDockWidgetArea);
    defaultLayout();
    setStatusBar(new QStatusBar);
    screenRec = new ScreenRecorder(this);
    hud = new RecordHud;

    // ---- связи ----
    connect(timeline, &TimelineWidget::playheadDragged, player, &PlayerView::seek);
    connect(player, &PlayerView::timeChanged, this, [this](double t) {
        timeline->setPlayhead(t);
        transport->setTime(t, project.duration());
        inspector->setNow(t);
    });
    connect(player, &PlayerView::playStateChanged, transport, &TransportBar::setPlaying);
    connect(player, &PlayerView::playStateChanged, this, [this](bool playing) { if (!playing && recActive) stopRecording(); });
    connect(player, &PlayerView::clipPicked, timeline, &TimelineWidget::selectClip);
    connect(player, &PlayerView::transformEdited, inspector, &Inspector::refreshValues);
    connect(player, &PlayerView::aboutToEdit, this, [this]() { pushUndo("player"); });
    connect(player, &PlayerView::countdownFinished, this, [this]() { beginRecording(); });
    connect(timeline, &TimelineWidget::selectionChanged, this, [this](ClipPtr c) { player->setSelected(c); inspector->setClip(c); });
    connect(timeline, &TimelineWidget::changed, this, [this]() { transport->setTime(player->time(), project.duration()); player->update(); });
    connect(timeline, &TimelineWidget::aboutToChange, this, [this]() { pushUndo(); });
    connect(timeline, &TimelineWidget::effectDropped, this, [this](ClipPtr c, QString spec) { applyToClip(c, spec); });
    connect(player, &PlayerView::effectDropped, this, [this](ClipPtr c, QString spec) { applyToClip(c, spec); });
    connect(timeline, &TimelineWidget::recordScreenRequested, this, [this]() { startScreenRecording(); });
    connect(mixer, &MixerPanel::openFx, this, [this](QString ch) {
        ChannelFxDialog* d = new ChannelFxDialog(&project, ch, this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        d->show();
    });
    connect(hud, &RecordHud::stopClicked, this, [this]() { stopScreenRecording(); });
    connect(screenRec, &ScreenRecorder::finished, this, [this](QString path) {
        hud->hide(); showNormal(); raise(); activateWindow();
        if (QFileInfo::exists(path)) importPaths(QStringList(path), timeline->playheadTime(), "V1", true);
        statusBar()->showMessage("Запись экрана добавлена на таймлайн", 5000);
    });
    connect(screenRec, &ScreenRecorder::failed, this, [this](QString m) {
        hud->hide(); showNormal();
        QMessageBox::warning(this, "Запись экрана", "Не удалось записать экран:\n" + m);
    });
    connect(timeline, &TimelineWidget::tracksChanged, this, [this]() { mixer->rebuild(); });
    connect(timeline, &TimelineWidget::filesDropped, this, [this](QStringList p, double t, QString tr) { importPaths(p, t, tr, true); });
    connect(timeline, &TimelineWidget::bladeToggled, transport, &TransportBar::setBlade);
    connect(timeline, &TimelineWidget::addTextRequested, this, [this](double t) { addText(t); });
    connect(timeline, &TimelineWidget::textEditRequested, this, [this](ClipPtr c) { editTextClip(c); });
    connect(timeline, &TimelineWidget::copyRequested, this, [this]() { copySelection(); });
    connect(timeline, &TimelineWidget::cutRequested, this, [this]() { copySelection(); timeline->deleteSelected(false); });
    connect(timeline, &TimelineWidget::pasteRequested, this, [this](double t) { pasteAt(t); });
    connect(timeline, &TimelineWidget::duplicateRequested, this, [this]() { duplicateSelection(); });
    connect(timeline, &TimelineWidget::recordVoiceRequested, this, [this](QString tr) {
        statusBar()->showMessage("Дорожка " + tr + " готова к записи: нажми красную кнопку у названия дорожки", 6000);
    });
    connect(timeline, &TimelineWidget::recordButtonClicked, this, [this](QString) { toggleRecording(); });
    connect(timeline, &TimelineWidget::inputGainChanged, this, [this](double g) { recorder->setGain(g); });
    connect(recorder, &VoiceRecorder::level, timeline, &TimelineWidget::setRecordLevel);
    connect(recorder, &VoiceRecorder::failed, this, [this](QString m) {
        recActive = false; player->setFreeRun(false); timeline->setRecording(false);
        QMessageBox::warning(this, "Запись голоса", m);
    });
    connect(recorder, &VoiceRecorder::finished, this, [this](QString path, double secs) {
        timeline->setRecording(false);
        player->setFreeRun(false);
        if (secs < 0.05) return;
        pushUndo();
        Asset* a = project.addAsset(path);
        if (!a) return;
        Asset asset = *a;
        refreshAssets();
        Peaks::instance().request(asset.path);
        QString tr = recTrack.isEmpty() ? QString("A1") : recTrack;
        project.addClipsForAsset(asset, recStart, tr);
        timeline->update(); player->update();
        transport->setTime(player->time(), project.duration());
        statusBar()->showMessage("Голос записан на дорожку " + tr, 4000);
    });
    connect(transport, &TransportBar::playToggled, player, &PlayerView::toggle);
    connect(transport, &TransportBar::seekRequested, player, &PlayerView::seek);
    connect(transport, &TransportBar::stepFrames, player, &PlayerView::stepFrames);
    connect(transport, &TransportBar::skipSeconds, this, [this](double s) { player->seek(player->time() + s); });
    connect(transport, &TransportBar::jumpCut, timeline, &TimelineWidget::jumpCut);
    connect(transport, &TransportBar::bladeToggled, timeline, &TimelineWidget::setBlade);
    connect(transport, &TransportBar::recToggled, player, &PlayerView::setRecord);
    connect(transport, &TransportBar::snapToggled, timeline, &TimelineWidget::setSnap);
    connect(transport, &TransportBar::addTextRequested, this, [this]() { addText(timeline->playheadTime()); });
    connect(assets, &AssetsPanel::filesAdded, this, [this](QStringList p) { importPaths(p, 0, "V1", false); });
    connect(assets, &AssetsPanel::assetActivated, this, [this](int id) {
        if (!project.assets.contains(id)) return;
        const Asset& a = project.assets[id];
        importPaths(QStringList(a.path), timeline->playheadTime(), a.kind == "audio" ? "A1" : "V1", true);
    });
    connect(fxTree, &EffectsTree::hovered, player, &PlayerView::setPreviewFx);
    connect(fxTree, &EffectsTree::applyEffect, this, &MainWindow::applyEffect);
    connect(fxTree, &EffectsTree::applyPreset, this, &MainWindow::applyPreset);
    connect(anims, &AnimationsList::applyAnimation, this, &MainWindow::applyAnim);
    connect(inspector, &Inspector::changed, this, [this]() { player->update(); timeline->update(); });
    connect(inspector, &Inspector::aboutToChange, this, [this](QString k) { pushUndo(k); });
    connect(inspector, &Inspector::presetSaved, fxTree, &EffectsTree::rebuild);

    buildMenus();

    connect(&autosaveTimer, &QTimer::timeout, this, &MainWindow::autosave);
    autosaveTimer.start(SettingsDialog::autosaveSeconds() * 1000);
    connect(&meterTimer, &QTimer::timeout, this, [this]() {
        if (mixer->isVisible()) mixer->updateMeters(player->audioEngine().levels(), player->isPlaying());
    });
    meterTimer.start(33);

    QSettings s("UltimateEditor", "UltimateEditor");
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("state4")) { if (!restoreState(s.value("state4").toByteArray(), 4)) defaultLayout(); }
    transport->setTime(0, 0);
}

QDockWidget* MainWindow::addDock(const QString& title, const QString& obj, QWidget* w, Qt::DockWidgetArea area) {
    Q_UNUSED(area);
    QDockWidget* d = new QDockWidget(title, this);
    d->setObjectName(obj);
    d->setWidget(w);
    d->setAllowedAreas(Qt::AllDockWidgetAreas);                 // любую панель можно стыковать к любой стороне
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    docks.append(d);
    return d;
}

void MainWindow::defaultLayout() {
    for (QDockWidget* d : docks) { d->setFloating(false); removeDockWidget(d); }
    addDockWidget(Qt::LeftDockWidgetArea, dockAssets);
    addDockWidget(Qt::RightDockWidgetArea, dInsp);
    addDockWidget(Qt::RightDockWidgetArea, dMix);
    addDockWidget(Qt::BottomDockWidgetArea, dTl);
    tabifyDockWidget(dInsp, dMix);
    for (QDockWidget* d : docks) d->show();
    dInsp->raise();
    resizeDocks({dockAssets, dInsp}, {320, 360}, Qt::Horizontal);
    resizeDocks({dTl}, {320}, Qt::Vertical);
}

void MainWindow::buildMenus() {
    // act(): действие с несколькими горячими клавишами (как в Premiere / Resolve / Vegas / Final Cut)
    auto act = [this](const QString& text, const QStringList& keys, std::function<void()> fn) {
        QAction* a = new QAction(text, this);
        QList<QKeySequence> ks;
        for (const QString& k : keys) ks << QKeySequence(k);
        if (!ks.isEmpty()) { a->setShortcuts(ks); a->setShortcutContext(Qt::ApplicationShortcut); }
        connect(a, &QAction::triggered, this, [fn]() { fn(); });
        addAction(a);
        return a;
    };
    QMenu* mf = menuBar()->addMenu("Файл");
    mf->addAction(act("Новый проект", {"Ctrl+N"}, [this]() { newProject(); }));
    mf->addAction(act("Открыть…", {"Ctrl+O"}, [this]() { openProject(); }));
    mf->addAction(act("Сохранить", {"Ctrl+S"}, [this]() { saveProject(false); }));
    mf->addAction(act("Сохранить как…", {"Ctrl+Shift+S"}, [this]() { saveProject(true); }));
    mf->addSeparator();
    mf->addAction(act("Импорт медиа…", {"Ctrl+I"}, [this]() { importDialog(); }));
    mf->addAction(act("Экспорт…", {"Ctrl+E", "Ctrl+M"}, [this]() { exportDialog(); }));
    mf->addSeparator();
    mf->addAction(act("Восстановить автосохранение", {}, [this]() { recover(); }));
    mf->addAction(act("Настройки (микрофон…)", {"Ctrl+,"}, [this]() {
        SettingsDialog d(this);
        if (d.exec()) autosaveTimer.start(SettingsDialog::autosaveSeconds() * 1000);
    }));
    mf->addAction(act("Выход", {"Ctrl+Q"}, [this]() { close(); }));

    QMenu* me = menuBar()->addMenu("Правка");
    me->addAction(act("Отменить", {"Ctrl+Z"}, [this]() { undo(); }));
    me->addAction(act("Повторить", {"Ctrl+Shift+Z", "Ctrl+Y"}, [this]() { redo(); }));
    me->addSeparator();
    me->addAction(act("Копировать", {"Ctrl+C"}, [this]() { copySelection(); }));
    me->addAction(act("Вырезать", {"Ctrl+X"}, [this]() { copySelection(); timeline->deleteSelected(false); }));
    me->addAction(act("Вставить", {"Ctrl+V"}, [this]() { pasteAt(timeline->playheadTime()); }));
    me->addAction(act("Дублировать", {"Ctrl+D"}, [this]() { duplicateSelection(); }));
    me->addAction(act("Выделить всё", {"Ctrl+A"}, [this]() { timeline->selectAll(); }));
    me->addSeparator();
    me->addAction(act("Удалить", {"Delete", "Backspace"}, [this]() { timeline->deleteSelected(false); }));
    me->addAction(act("Удалить со сдвигом (Ripple)", {"Shift+Delete"}, [this]() { timeline->deleteSelected(true); }));
    me->addAction(act("Разделить на плейхеде", {"S", "Ctrl+K", "Ctrl+B"}, [this]() { timeline->splitAtPlayhead(); }));
    me->addAction(act("Ножницы (вкл/выкл)", {"C", "B"}, [this]() { timeline->setBlade(!timeline->bladeMode()); }));
    me->addAction(act("Инструмент «Выбор»", {"V"}, [this]() { timeline->setBlade(false); }));
    me->addAction(act("Маркер", {"M"}, [this]() { timeline->addMarker(); }));
    me->addAction(act("Добавить текст…", {"T"}, [this]() { addText(timeline->playheadTime()); }));
    me->addAction(act("Создать прокси для выбранного видео", {}, [this]() {
        for (const ClipPtr& c : timeline->selection()) if (c->kind == "video") { makeProxy(c->assetId); break; }
    }));
    act("Отмена / стоп записи", {"Esc"}, [this]() {
        if (player->countingDown()) { player->cancelCountdown(); }
        else if (recActive) stopRecording();
        else timeline->setBlade(false);
    });

    QMenu* mp = menuBar()->addMenu("Воспроизведение");
    mp->addAction(act("Play / Pause", {"Space"}, [this]() { player->toggle(); }));
    mp->addAction(act("Играть (L)", {"L"}, [this]() { if (!player->isPlaying()) player->play(); }));
    mp->addAction(act("Пауза (K)", {"K"}, [this]() { player->pause(); }));
    mp->addAction(act("Назад 1 с (J)", {"J", "Shift+Left"}, [this]() { player->seek(player->time() - 1.0); }));
    mp->addAction(act("Вперёд 1 с", {"Shift+Right"}, [this]() { player->seek(player->time() + 1.0); }));
    mp->addAction(act("Кадр назад", {"Left", ","}, [this]() { player->stepFrames(-1); }));
    mp->addAction(act("Кадр вперёд", {"Right", "."}, [this]() { player->stepFrames(1); }));
    mp->addAction(act("К предыдущей склейке", {"Up"}, [this]() { timeline->jumpCut(-1); }));
    mp->addAction(act("К следующей склейке", {"Down"}, [this]() { timeline->jumpCut(1); }));
    mp->addAction(act("В начало", {"Home"}, [this]() { player->seek(0); }));
    mp->addAction(act("В конец", {"End"}, [this]() { player->seek(project.duration()); }));

    QMenu* mv = menuBar()->addMenu("Вид");
    for (QDockWidget* d : docks) mv->addAction(d->toggleViewAction());
    mv->addSeparator();
    QMenu* place = mv->addMenu("Расположить окно");
    for (QDockWidget* d : docks) {
        QMenu* m = place->addMenu(d->windowTitle());
        auto addArea = [this, d, m](const QString& text, Qt::DockWidgetArea area) {
            QAction* a = m->addAction(text);
            connect(a, &QAction::triggered, this, [this, d, area]() { d->setFloating(false); addDockWidget(area, d); d->show(); d->raise(); });
        };
        addArea("⬅ Прикрепить слева", Qt::LeftDockWidgetArea);
        addArea("➡ Прикрепить справа", Qt::RightDockWidgetArea);
        addArea("⬆ Прикрепить сверху", Qt::TopDockWidgetArea);
        addArea("⬇ Прикрепить снизу", Qt::BottomDockWidgetArea);
        QAction* fl = m->addAction("🗗 Отсоединить в отдельное окно");
        connect(fl, &QAction::triggered, this, [d]() { d->setFloating(true); d->show(); });
        m->addSeparator();
        for (QDockWidget* o : docks) {
            if (o == d) continue;
            QAction* a = m->addAction("Вкладкой к «" + o->windowTitle() + "»");
            connect(a, &QAction::triggered, this, [this, d, o]() {
                d->setFloating(false);
                Qt::DockWidgetArea ar = dockWidgetArea(o);
                addDockWidget(ar == Qt::NoDockWidgetArea ? Qt::LeftDockWidgetArea : ar, d);
                tabifyDockWidget(o, d);
                d->show(); d->raise();
            });
        }
    }
    QMenu* sub = mv->addMenu("Ассеты: вкладка");
    const QStringList tabs = {"Медиа", "Анимации", "Шейдеры"};
    for (int i = 0; i < tabs.size(); ++i)
        sub->addAction(act(tabs[i], {}, [this, i]() { dockAssets->show(); dockAssets->raise(); assetTabs->setCurrentIndex(i); }));
    mv->addAction(act("Показать все окна", {}, [this]() { showAllDocks(); }));
    QMenu* ml = mv->addMenu("Раскладки окон");
    connect(ml, &QMenu::aboutToShow, this, [this, ml]() {
        ml->clear();
        QAction* reset = ml->addAction("Сбросить по умолчанию");
        connect(reset, &QAction::triggered, this, [this]() { defaultLayout(); });
        QAction* save = ml->addAction("Сохранить текущую раскладку…");
        connect(save, &QAction::triggered, this, [this]() {
            bool ok = false;
            QString n = QInputDialog::getText(this, "Раскладка", "Название раскладки:", QLineEdit::Normal, "", &ok);
            if (ok && !n.isEmpty()) QSettings("UltimateEditor", "UltimateEditor").setValue("layouts/" + n, saveState(4));
        });
        ml->addSeparator();
        QSettings s("UltimateEditor", "UltimateEditor");
        s.beginGroup("layouts");
        for (const QString& key : s.childKeys()) {
            QByteArray st = s.value(key).toByteArray();
            QAction* a = ml->addAction(key);
            connect(a, &QAction::triggered, this, [this, st]() { restoreState(st, 4); });
        }
    });
    mv->addSeparator();
    mv->addAction(act("Приблизить таймлайн", {"Ctrl+=", "Ctrl++"}, [this]() { timeline->zoomBy(1.4); }));
    mv->addAction(act("Отдалить таймлайн", {"Ctrl+-"}, [this]() { timeline->zoomBy(1 / 1.4); }));
    mv->addAction(act("Весь проект на таймлайне", {"Shift+Z", "\\"}, [this]() { timeline->zoomFit(); }));

    QMenu* mr = menuBar()->addMenu("Запись");
    mr->addAction(act("Записать голос…", {}, [this]() {
        QMessageBox::information(this, "Запись голоса", "Нажми правой кнопкой мыши на аудиодорожку на таймлайне и выбери «Записать голос».\nМикрофон выбирается в Файл → Настройки.");
    }));
    mr->addAction(act("Записать экран…", {"Ctrl+Shift+R"}, [this]() { startScreenRecording(); }));
    mr->addAction(act("Остановить запись экрана", {}, [this]() { stopScreenRecording(); }));

    QMenu* mc = menuBar()->addMenu("Цвет");
    mc->addAction(act("Color Wheels (Lift/Gamma/Gain)", {}, [this]() { applyEffect("Color Wheels"); }));
    mc->addAction(act("Brightness & Contrast", {}, [this]() { applyEffect("Brightness/Contrast"); }));
    mc->addAction(act("Saturation", {}, [this]() { applyEffect("Saturation"); }));

    QMenu* mh = menuBar()->addMenu("Справка");
    mh->addAction(act("Горячие клавиши", {"F1"}, [this]() { showShortcuts(); }));
}

void MainWindow::showShortcuts() {
    QMessageBox::information(this, "Горячие клавиши",
        "ФАЙЛ: Ctrl+N новый · Ctrl+O открыть · Ctrl+S сохранить · Ctrl+Shift+S сохранить как · Ctrl+I импорт · Ctrl+E / Ctrl+M экспорт\n\n"
        "ПРАВКА: Ctrl+Z отменить · Ctrl+Shift+Z / Ctrl+Y повторить · Ctrl+C копировать · Ctrl+X вырезать · Ctrl+V вставить · "
        "Ctrl+D дублировать · Ctrl+A выделить всё · Delete/Backspace удалить · Shift+Delete удалить со сдвигом\n\n"
        "МОНТАЖ: C или B ножницы (клик там, где резать) · S / Ctrl+K / Ctrl+B разрезать на плейхеде · V инструмент «Выбор» · "
        "M маркер · T текст · Esc отмена\n\n"
        "ВОСПРОИЗВЕДЕНИЕ: Space играть/пауза · K пауза · L играть · J назад 1 с · ←/→ кадр · Shift+←/→ ±1 с · ↑/↓ склейки · Home/End\n\n"
        "ВИД: Ctrl+= приблизить · Ctrl+- отдалить · Shift+Z весь проект · Ctrl+, настройки (микрофон) · F1 эта справка\n\n"
        "МЫШЬ: ПКМ по таймлайну — меню (добавить дорожку, записать голос, текст, убрать эффект…) · "
        "белые кружки на клипе — затухание · голубые — длительность анимаций · ✕ на плашке убирает эффект/анимацию");
}

void MainWindow::showAllDocks() {
    for (QDockWidget* d : docks) { d->setFloating(false); d->show(); }
}

// ---------- Undo / Redo ----------
void MainWindow::pushUndo(const QString& key) {
    if (!key.isEmpty() && key == undoKey && undoClock.isValid() && undoClock.elapsed() < 1000) { undoClock.restart(); return; }
    undoStack.append(project.toBytes());
    if (undoStack.size() > 100) undoStack.removeFirst();
    redoStack.clear();
    undoKey = key;
    undoClock.restart();
}

void MainWindow::restore(const QByteArray& snap) {
    int keepNext = project.nextId;
    QString path = project.path;
    project.fromBytes(snap);
    project.nextId = qMax(project.nextId, keepNext);          // id клипов не переиспользуются (кэш декодеров)
    project.path = path;
    timeline->setProject(&project);
    mixer->setProject(&project);
    inspector->setClip(ClipPtr());
    player->setSelected(ClipPtr());
    refreshAssets();
    transport->setTime(player->time(), project.duration());
    player->update();
}

void MainWindow::undo() {
    if (undoStack.isEmpty()) { statusBar()->showMessage("Нечего отменять", 2000); return; }
    redoStack.append(project.toBytes());
    QByteArray s = undoStack.takeLast();
    undoKey.clear();
    restore(s);
}

void MainWindow::redo() {
    if (redoStack.isEmpty()) { statusBar()->showMessage("Нечего повторять", 2000); return; }
    undoStack.append(project.toBytes());
    QByteArray s = redoStack.takeLast();
    undoKey.clear();
    restore(s);
}

// ---------- копировать / вставить ----------
void MainWindow::copySelection() {
    clipboard.clear();
    for (const ClipPtr& c : timeline->selection()) clipboard.append(*c);
    statusBar()->showMessage(QString("Скопировано клипов: %1").arg(clipboard.size()), 2000);
}

void MainWindow::pasteAt(double t) {
    if (clipboard.isEmpty()) return;
    pushUndo();
    double minStart = clipboard[0].start;
    for (const Clip& c : clipboard) minStart = qMin(minStart, c.start);
    QMap<int, int> linkMap;
    ClipPtr first;
    for (const Clip& src : clipboard) {
        ClipPtr n = std::make_shared<Clip>(src);
        n->id = project.nextId++;
        n->start = src.start - minStart + t;
        if (src.link) {
            if (!linkMap.contains(src.link)) linkMap[src.link] = project.nextId++;
            n->link = linkMap[src.link];
        }
        if (!project.tracks.contains(n->track)) n->track = isVideoTrack(n->track) ? "V1" : "A1";
        project.clips.append(n);
        if (!first) first = n;
    }
    timeline->selectClip(first);
    transport->setTime(player->time(), project.duration());
    player->update();
}

void MainWindow::duplicateSelection() {
    if (timeline->selection().isEmpty()) return;
    double end = 0;
    for (const ClipPtr& c : timeline->selection()) end = qMax(end, c->end());
    copySelection();
    pasteAt(end);
}

// ---------- запись голоса ----------
void MainWindow::toggleRecording() {
    if (recActive) { stopRecording(); return; }
    if (player->countingDown()) { player->cancelCountdown(); return; }
    QString tr = timeline->armedTrack();
    if (tr.isEmpty()) return;
    if (SettingsDialog::selectedMic().isNull()) {
        QMessageBox::warning(this, "Запись голоса", "Микрофон не найден. Выбери его в Файл → Настройки.");
        return;
    }
    recTrack = tr;
    recStart = player->time();
    player->pause();
    player->startCountdown(3);                                 // 3 секунды отсчёта на видео
}

void MainWindow::beginRecording() {
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recordings");
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recordings/voice_"
                   + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".wav";
    recorder->setGain(timeline->inputGain());
    if (!recorder->start(SettingsDialog::selectedMic(), path)) return;
    recActive = true;
    timeline->setRecording(true);
    player->setFreeRun(true);
    player->seek(recStart);
    player->play();                                            // видео идёт, микрофон пишет
}

void MainWindow::stopRecording() {
    if (!recActive) return;
    recActive = false;
    player->pause();
    recorder->stop();                                          // finished() создаст клип
}

// ---------- проект ----------
void MainWindow::refreshAssets() {
    assets->clearAll();
    for (auto it = project.assets.constBegin(); it != project.assets.constEnd(); ++it) assets->addAsset(it.value());
}

void MainWindow::setProject() {
    player->setProject(&project);
    timeline->setProject(&project);
    mixer->setProject(&project);
    inspector->setClip(ClipPtr());
    refreshAssets();
    for (auto it = project.assets.constBegin(); it != project.assets.constEnd(); ++it)
        if (it.value().hasAudio) Peaks::instance().request(it.value().path);
    undoStack.clear(); redoStack.clear();
    transport->setTime(0, project.duration());
    setWindowTitle("Ultimate Video Editor" + (project.path.isEmpty() ? QString() : " — " + QFileInfo(project.path).fileName()));
}

void MainWindow::newProject() { project = Project(); setProject(); }

bool MainWindow::openProjectFile(const QString& f) {
    if (!project.load(f)) { QMessageBox::warning(this, "Ошибка", "Не удалось открыть проект:\n" + f); return false; }
    addRecentProject(f);
    setProject();
    return true;
}

void MainWindow::openProject() {
    QString f = QFileDialog::getOpenFileName(this, "Открыть проект", QString(), "Проект Ultimate Video Editor (*.uvmvideos)");
    if (!f.isEmpty()) openProjectFile(f);
}

void MainWindow::saveProject(bool saveAs) {
    QString f = project.path;
    if (f.isEmpty() || saveAs) f = QFileDialog::getSaveFileName(this, "Сохранить проект", "project.uvmvideos", "Проект Ultimate Video Editor (*.uvmvideos)");
    if (f.isEmpty()) return;
    if (!f.endsWith(".uvmvideos")) f += ".uvmvideos";
    if (project.save(f)) {
        project.path = f;
        addRecentProject(f);
        setWindowTitle("Ultimate Video Editor — " + QFileInfo(f).fileName());
        statusBar()->showMessage("Сохранено: " + f, 3000);
    }
}

static QString autosavePath() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d);
    return d + "/autosave.uvmvideos";
}
void MainWindow::autosave() { if (!project.clips.isEmpty()) project.save(autosavePath()); }
void MainWindow::recover() {
    if (!QFileInfo::exists(autosavePath())) { QMessageBox::information(this, "Автосохранение", "Файл автосохранения не найден"); return; }
    if (project.load(autosavePath())) { project.path.clear(); setProject(); }
}

// ---------- импорт ----------
void MainWindow::importDialog() {
    QStringList fs = QFileDialog::getOpenFileNames(this, "Импорт медиа", QString(), "Медиа (*.*)");
    if (!fs.isEmpty()) importPaths(fs, 0, "V1", false);
}

void MainWindow::importPaths(const QStringList& paths, double t, const QString& track, bool toTimeline) {
    pushUndo();
    for (const QString& path : paths) {
        Asset* a = project.addAsset(path);
        if (!a) continue;
        Asset asset = *a;
        assets->addAsset(asset);
        if (asset.hasAudio) Peaks::instance().request(asset.path);
        if (asset.kind == "video" && asset.w > 0 && project.clips.isEmpty()) {
            double k = qMin(1.0, 1920.0 / asset.w);
            project.W = ((int)(asset.w * k) / 2) * 2;
            project.H = ((int)(asset.h * k) / 2) * 2;
        }
        if (toTimeline) {
            project.addClipsForAsset(asset, t, project.tracks.contains(track) ? track : QString("V1"));
            t += asset.kind == "image" ? 5.0 : asset.duration;
        }
        if (asset.kind == "video" && asset.h > 1440) makeProxy(asset.id);
    }
    transport->setTime(player->time(), project.duration());
    timeline->update();
    player->update();
}

void MainWindow::makeProxy(int id) {
    if (!project.assets.contains(id)) return;
    QString src = project.assets[id].path;
    QString out = cacheDir + "/" + QFileInfo(src).fileName() + ".proxy.mp4";
    if (QFileInfo::exists(out)) { project.assets[id].proxy = out; return; }
    statusBar()->showMessage("Создаю прокси…");
    QProcess* p = new QProcess(this);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, p, id, out](int code, QProcess::ExitStatus) {
        if (code == 0 && project.assets.contains(id)) project.assets[id].proxy = out;
        statusBar()->showMessage(code == 0 ? "Прокси готово" : "Прокси не создано", 3000);
        p->deleteLater();
    });
    p->start(ffmpegPath(), QStringList() << "-y" << "-v" << "error" << "-i" << src << "-vf" << "scale=-2:540"
                                     << "-c:v" << "libx264" << "-crf" << "26" << "-preset" << "veryfast" << "-g" << "15" << "-an" << out);
}

void MainWindow::exportDialog() {
    player->pause();
    ExportDialog d(&project, player, this);
    d.exec();
}

// ---------- эффекты / анимации / текст ----------
ClipPtr MainWindow::target() {
    for (const ClipPtr& c : timeline->selection()) if (c->kind != "audio") return c;
    ClipPtr best;
    for (const ClipPtr& c : project.clipsAt(player->time())) if (c->kind != "audio") best = c;
    return best;
}

ClipPtr MainWindow::selectedOrAtPlayhead() {
    for (const ClipPtr& c : timeline->selection()) if (c->kind == "audio") return c;
    if (!timeline->selection().isEmpty()) return timeline->selection().first();
    for (const ClipPtr& c : project.clipsAt(player->time())) if (c->kind == "audio") return c;
    return ClipPtr();
}

void MainWindow::applyToClip(const ClipPtr& c, const QString& spec) {
    if (!c) { statusBar()->showMessage("Выбери клип на таймлайне или перетащи эффект на клип", 3500); return; }
    const QString kind = spec.section(':', 0, 0), name = spec.section(':', 1);
    ClipPtr dst = c;
    if (kind == "afx" && c->kind != "audio") {                    // звуковой эффект, брошенный на видео -> на его звук
        dst.reset();
        if (c->link) for (const ClipPtr& o : project.clips) if (o->link == c->link && o->kind == "audio") { dst = o; break; }
        if (!dst) { statusBar()->showMessage("У этого клипа нет звука: перетащи звуковой эффект на аудиоклип", 4000); return; }
    }
    if ((kind == "fx" || kind == "anim" || kind == "preset") && c->kind == "audio") {
        statusBar()->showMessage("Видео-эффекты и анимации применяются к видео, фото и тексту", 4000);
        return;
    }
    pushUndo();
    if (kind == "fx" || kind == "afx") {
        const QList<ParamDef>* ps = effectParams(name);
        if (!ps) return;
        EffectInst e; e.name = name;
        for (const ParamDef& pd : *ps) e.params[pd.name] = pd.def;
        dst->effects.push_back(e);
    } else if (kind == "anim") {
        setAnimation(*dst, name);
    } else if (kind == "preset") {
        for (const EffectInst& e : loadPreset(name)) dst->effects.push_back(e);
    }
    timeline->selectClip(dst);                                    // выделяем и показываем в инспекторе
    timeline->update(); player->update();
}

void MainWindow::applyEffect(const QString& name) {
    bool audio = isAudioEffect(name);
    applyToClip(audio ? selectedOrAtPlayhead() : target(), (audio ? "afx:" : "fx:") + name);
}
void MainWindow::applyPreset(const QString& name) { applyToClip(target(), "preset:" + name); }
void MainWindow::applyAnim(const QString& name) { applyToClip(target(), "anim:" + name); }

// ---------- запись экрана ----------
void MainWindow::startScreenRecording() {
    if (screenRec->isRecording()) return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/recordings";
    QDir().mkpath(dir);
    screenPath = dir + "/screen_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".mp4";
    bool mic = QMessageBox::question(this, "Запись экрана", "Записывать звук с микрофона?\n(Микрофон выбирается в Файл → Настройки)") == QMessageBox::Yes;
    showMinimized();                                              // убираем редактор с экрана, запись начнётся через 1.5 с
    QTimer::singleShot(1500, this, [this, mic]() {
        if (!screenRec->start(screenPath, mic, SettingsDialog::selectedMic())) { showNormal(); return; }
        hud->begin();
    });
}

void MainWindow::stopScreenRecording() {
    if (!screenRec->isRecording()) return;
    hud->hide();
    screenRec->stop();
}

void MainWindow::addText(double t) {
    bool ok = false;
    QString txt = QInputDialog::getText(this, "Текст", "Текст:", QLineEdit::Normal, "", &ok);
    if (!ok || txt.isEmpty()) return;
    pushUndo();
    ClipPtr c = project.addTextClip(txt, t, 3.0);
    timeline->selectClip(c);
    transport->setTime(player->time(), project.duration());
    player->update();
}

void MainWindow::editTextClip(const ClipPtr& c) {
    if (!c || c->kind != "text") return;
    bool ok = false;
    QString txt = QInputDialog::getText(this, "Текст", "Текст:", QLineEdit::Normal, c->text, &ok);
    if (!ok) return;
    pushUndo();
    c->text = txt;
    inspector->setClip(c);
    timeline->update(); player->update();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (recActive) stopRecording();
    player->pause();
    QSettings s("UltimateEditor", "UltimateEditor");
    s.setValue("geometry", saveGeometry());
    s.setValue("state4", saveState(4));
    if (hud) hud->close();
    autosave();
    QMainWindow::closeEvent(e);
}
