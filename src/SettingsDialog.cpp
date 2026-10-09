#include "SettingsDialog.h"
#include "AppSettings.h"
#include "Theme.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFontComboBox>
#include <QKeySequenceEdit>
#include <QAction>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QMediaDevices>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QDir>
#include <QSettings>
#include <QFormLayout>

QWidget* SettingsDialog::makePage(const QString& title, QFormLayout*& form) {
    QWidget* w = new QWidget;
    QVBoxLayout* v = new QVBoxLayout(w);
    v->addWidget(new QLabel("<span style='font-size:16px;font-weight:bold;'>" + title + "</span>"));
    QWidget* body = new QWidget;
    form = new QFormLayout(body);
    v->addWidget(body);
    v->addStretch(1);
    pages->addWidget(w);
    nav->addItem(title);
    return w;
}

SettingsDialog::SettingsDialog(QWidget* parent, const QList<QAction*>& actions) : QDialog(parent), acts(actions) {
    setWindowTitle("Настройки программы");
    resize(900, 620);
    QVBoxLayout* root = new QVBoxLayout(this);
    QHBoxLayout* mid = new QHBoxLayout;
    nav = new QListWidget; nav->setFixedWidth(190);
    pages = new QStackedWidget;
    mid->addWidget(nav); mid->addWidget(pages, 1);
    root->addLayout(mid, 1);
    QFormLayout* f = nullptr;

    // ---- общие ----
    makePage("Общие", f);
    autosave = new QSpinBox; autosave->setRange(10, 3600); autosave->setSuffix(" с"); autosave->setValue(AppSettings::autosaveSec());
    undoLimit = new QSpinBox; undoLimit->setRange(10, 500); undoLimit->setValue(AppSettings::undoLimit());
    startScreen = new QCheckBox("Показывать стартовое окно (недавние проекты)"); startScreen->setChecked(AppSettings::showStartScreen());
    imageSec = new QDoubleSpinBox; imageSec->setRange(0.5, 60); imageSec->setSuffix(" с"); imageSec->setValue(AppSettings::imageDuration());
    f->addRow("Автосохранение каждые", autosave);
    f->addRow("Шагов отмены (Ctrl+Z)", undoLimit);
    f->addRow("Длительность фото на таймлайне", imageSec);
    f->addRow(startScreen);

    // ---- тема ----
    makePage("Тема и шрифты", f);
    themeBox = new QComboBox; themeBox->addItems({"Тёмная", "Светлая", "Своя"});
    const QString mode = Theme::instance().mode();
    themeBox->setCurrentIndex(mode == "light" ? 1 : mode == "custom" ? 2 : 0);
    custom = Theme::instance().colors();
    f->addRow("Тема", themeBox);
    for (const QString& k : Theme::instance().keys()) {
        QPushButton* b = new QPushButton;
        colorBtns[k] = b;
        f->addRow(Theme::label(k), b);
        connect(b, &QPushButton::clicked, this, [this, k]() {
            QColor c = QColorDialog::getColor(custom.value(k), this, Theme::label(k));
            if (!c.isValid()) return;
            custom[k] = c;
            themeBox->setCurrentIndex(2);
            refreshColorButtons();
        });
    }
    uiFont = new QFontComboBox;
    QString fam = AppSettings::get("ui/font_family", "").toString();
    if (!fam.isEmpty()) uiFont->setCurrentFont(QFont(fam));
    uiFontSize = new QSpinBox; uiFontSize->setRange(7, 24); uiFontSize->setValue(AppSettings::get("ui/font_size", 10).toInt());
    f->addRow("Шрифт интерфейса", uiFont);
    f->addRow("Размер шрифта", uiFontSize);
    QPushButton* exp = new QPushButton("Сохранить свою тему в файл…"), *imp = new QPushButton("Загрузить тему из файла…"), *now = new QPushButton("Применить сейчас");
    f->addRow(exp); f->addRow(imp); f->addRow(now);
    connect(themeBox, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (i == 0) custom = Theme::preset("dark");
        else if (i == 1) custom = Theme::preset("light");
        refreshColorButtons();
    });
    connect(now, &QPushButton::clicked, this, [this]() { save(); });
    connect(exp, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getSaveFileName(this, "Тема", "my-theme.json", "Тема (*.json)");
        if (file.isEmpty()) return;
        Theme::instance().setCustom(custom);
        Theme::instance().exportTo(file);
    });
    connect(imp, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, "Тема", QString(), "Тема (*.json)");
        if (file.isEmpty()) return;
        if (Theme::instance().importFrom(file)) { custom = Theme::instance().colors(); themeBox->setCurrentIndex(2); refreshColorButtons(); }
    });
    refreshColorButtons();

    // ---- таймлайн ----
    makePage("Таймлайн", f);
    trackH = new QSpinBox; trackH->setRange(30, 140); trackH->setSuffix(" px"); trackH->setValue(AppSettings::trackHeight());
    snapPx = new QSpinBox; snapPx->setRange(2, 40); snapPx->setSuffix(" px"); snapPx->setValue(AppSettings::snapPixels());
    waves = new QCheckBox("Показывать визуализатор звука на клипах"); waves->setChecked(AppSettings::showWaveforms());
    f->addRow("Высота дорожек", trackH); f->addRow("Дальность магнитной привязки", snapPx); f->addRow(waves);

    // ---- плеер ----
    makePage("Плеер", f);
    loop = new QCheckBox("Повторять воспроизведение (цикл)"); loop->setChecked(AppSettings::loopPlayback());
    grid = new QCheckBox("Сетка «правило третей»"); grid->setChecked(AppSettings::showGrid());
    safe = new QCheckBox("Безопасные зоны (90% и 80%)"); safe->setChecked(AppSettings::showSafeZones());
    f->addRow(loop); f->addRow(grid); f->addRow(safe);

    // ---- звук ----
    makePage("Звук", f);
    outDev = new QComboBox; micDev = new QComboBox;
    outDev->addItem("(по умолчанию)", QByteArray());
    for (const QAudioDevice& d : QMediaDevices::audioOutputs()) outDev->addItem(d.description(), d.id());
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) micDev->addItem(d.description(), d.id());
    int oi = outDev->findData(AppSettings::audioOutputId()); if (oi >= 0) outDev->setCurrentIndex(oi);
    int mi = micDev->findData(AppSettings::get("mic_id").toByteArray()); if (mi >= 0) micDev->setCurrentIndex(mi);
    latency = new QSpinBox; latency->setRange(20, 500); latency->setSuffix(" мс"); latency->setValue(AppSettings::audioLatencyMs());
    f->addRow("Колонки / наушники", outDev); f->addRow("Микрофон для записи голоса", micDev); f->addRow("Задержка звука (буфер)", latency);
    f->addRow(new QLabel("Колонки и задержка применяются после перезапуска."));

    // ---- производительность ----
    makePage("Производительность", f);
    proxyH = new QSpinBox; proxyH->setRange(480, 4320); proxyH->setSuffix(" px"); proxyH->setValue(AppSettings::proxyThreshold());
    f->addRow("Делать прокси для видео выше", proxyH);
    f->addRow(new QLabel("Прокси — лёгкая копия для плавного монтажа 4K/8K. Экспорт всегда идёт в полном качестве."));

    // ---- горячие клавиши ----
    QWidget* kp = makePage("Горячие клавиши", f);
    QVBoxLayout* kv = qobject_cast<QVBoxLayout*>(kp->layout());
    QTableWidget* table = new QTableWidget(acts.size(), 2);
    table->setHorizontalHeaderLabels({"Действие", "Клавиши"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int i = 0; i < acts.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(acts[i]->text().remove('&')));
        QKeySequenceEdit* ke = new QKeySequenceEdit(acts[i]->shortcut().isEmpty() && !acts[i]->shortcuts().isEmpty() ? acts[i]->shortcuts().first() : acts[i]->shortcut());
        keyEdits.append(ke);
        table->setCellWidget(i, 1, ke);
    }
    table->resizeColumnToContents(0);
    kv->insertWidget(1, table, 1);

    // ---- пути ----
    makePage("Папки", f);
    auto pathRow = [&](const QString& label, const QString& path) {
        QPushButton* b = new QPushButton("Открыть: " + path);
        connect(b, &QPushButton::clicked, this, [path]() { QDir().mkpath(path); QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        f->addRow(label, b);
    };
    pathRow("Настройки и шрифты", QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    pathRow("Записи и автосохранение", QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    pathRow("Кэш и прокси", QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

    // ---- дополнительно ----
    makePage("Дополнительно", f);
    QPushButton* reset = new QPushButton("Сбросить ВСЕ настройки программы…");
    f->addRow(reset);
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, "Сброс", "Вернуть все настройки (тема, клавиши, звук, окна) к начальным? Проекты не удаляются.") != QMessageBox::Yes) return;
        QSettings("UltimateEditor", "UltimateEditor").clear();
        QMessageBox::information(this, "Сброс", "Готово. Перезапусти программу.");
    });

    nav->setCurrentRow(0);
    connect(nav, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this]() { save(); accept(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

void SettingsDialog::refreshColorButtons() {
    for (auto it = colorBtns.begin(); it != colorBtns.end(); ++it) {
        QColor c = custom.value(it.key());
        it.value()->setText(c.name());
        it.value()->setStyleSheet("QPushButton{background:" + c.name() + ";color:" + (c.lightness() > 128 ? "#000" : "#fff") + ";border:1px solid #888;padding:4px;}");
    }
}

void SettingsDialog::save() {
    AppSettings::set("autosave_sec", autosave->value());
    AppSettings::set("general/undo_limit", undoLimit->value());
    AppSettings::set("general/start_screen", startScreen->isChecked());
    AppSettings::set("general/image_sec", imageSec->value());
    AppSettings::set("timeline/track_height", trackH->value());
    AppSettings::set("timeline/snap_px", snapPx->value());
    AppSettings::set("timeline/waveforms", waves->isChecked());
    AppSettings::set("player/loop", loop->isChecked());
    AppSettings::set("player/grid", grid->isChecked());
    AppSettings::set("player/safe", safe->isChecked());
    AppSettings::set("audio/output_id", outDev->currentData().toByteArray());
    AppSettings::set("mic_id", micDev->currentData().toByteArray());
    AppSettings::set("audio/latency_ms", latency->value());
    AppSettings::set("perf/proxy_height", proxyH->value());
    // тема и шрифт
    AppSettings::set("ui/font_family", uiFont->currentFont().family());
    AppSettings::set("ui/font_size", uiFontSize->value());
    int ti = themeBox->currentIndex();
    if (ti == 2) Theme::instance().setCustom(custom);
    else Theme::instance().setMode(ti == 1 ? "light" : "dark");
    // горячие клавиши
    for (int i = 0; i < acts.size() && i < keyEdits.size(); ++i) {
        QKeySequence ks = keyEdits[i]->keySequence();
        QKeySequence old = acts[i]->shortcuts().isEmpty() ? QKeySequence() : acts[i]->shortcuts().first();
        if (ks != old) {
            AppSettings::set("shortcut/" + acts[i]->text(), ks.toString());
            acts[i]->setShortcut(ks);
        }
    }
}

QAudioDevice SettingsDialog::selectedMic() {
    QByteArray id = AppSettings::get("mic_id").toByteArray();
    const QList<QAudioDevice> list = QMediaDevices::audioInputs();
    for (const QAudioDevice& d : list) if (d.id() == id) return d;
    return QMediaDevices::defaultAudioInput();
}

int SettingsDialog::autosaveSeconds() { return AppSettings::autosaveSec(); }
