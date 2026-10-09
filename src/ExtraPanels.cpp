#include <QPolygonF>
#include "ExtraPanels.h"
#include "Effects.h"
#include "Fonts.h"
#include "AppSettings.h"
#include <QListWidget>
#include <QTreeWidget>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLineEdit>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QColorDialog>
#include <QFileSystemModel>
#include <QTreeView>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QPixmap>

static QString fmtT(double t) {
    int m = (int)(t / 60);
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(t - m * 60, 5, 'f', 2, QChar('0'));
}

// ================= Маркеры =================
MarkersPanel::MarkersPanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    QVBoxLayout* l = new QVBoxLayout(this);
    list = new QListWidget;
    QHBoxLayout* b = new QHBoxLayout;
    QPushButton* add = new QPushButton("＋ Маркер на плейхеде"), *del = new QPushButton("Удалить"), *clr = new QPushButton("Очистить все");
    b->addWidget(add); b->addWidget(del); b->addWidget(clr);
    l->addWidget(list, 1); l->addLayout(b);
    connect(add, &QPushButton::clicked, this, [this]() { emit addRequested(); refresh(); });
    connect(del, &QPushButton::clicked, this, [this]() {
        int r = list->currentRow();
        if (r >= 0 && r < project->markers.size()) { project->markers.removeAt(r); sig.clear(); refresh(); emit changed(); }
    });
    connect(clr, &QPushButton::clicked, this, [this]() { project->markers.clear(); sig.clear(); refresh(); emit changed(); });
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { emit seekRequested(it->data(Qt::UserRole).toDouble()); });
    refresh();
}

void MarkersPanel::refresh() {
    QString s;
    for (const Marker& m : project->markers) s += QString::number(m.t, 'f', 3) + m.color + ";";
    if (s == sig) return;
    sig = s;
    list->clear();
    for (const Marker& m : project->markers) {
        QListWidgetItem* it = new QListWidgetItem("● " + fmtT(m.t));
        it->setForeground(QColor(m.color));
        it->setData(Qt::UserRole, m.t);
        list->addItem(it);
    }
}

// ================= Свойства проекта =================
ProjectPropsPanel::ProjectPropsPanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    QFormLayout* f = new QFormLayout(this);
    preset = new QComboBox;
    preset->addItems({"Свой размер", "1920×1080 (16:9 Full HD)", "1280×720 (16:9 HD)", "3840×2160 (4K)", "1080×1920 (9:16 вертикальное)",
                      "1080×1080 (1:1 квадрат)", "1080×1350 (4:5)", "2560×1080 (21:9 кино)", "640×480 (4:3)"});
    w = new QSpinBox; w->setRange(16, 16384); w->setSingleStep(2);
    h = new QSpinBox; h->setRange(16, 16384); h->setSingleStep(2);
    fps = new QComboBox; fps->addItems({"24", "25", "30", "50", "60"});
    bg = new QPushButton("Цвет фона кадра");
    QPushButton* apply = new QPushButton("Применить");
    f->addRow("Шаблон", preset); f->addRow("Ширина", w); f->addRow("Высота", h); f->addRow("Кадров/с", fps); f->addRow(bg); f->addRow(apply);
    connect(preset, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        static const int sz[][2] = {{0, 0}, {1920, 1080}, {1280, 720}, {3840, 2160}, {1080, 1920}, {1080, 1080}, {1080, 1350}, {2560, 1080}, {640, 480}};
        if (i > 0) { w->setValue(sz[i][0]); h->setValue(sz[i][1]); }
    });
    connect(bg, &QPushButton::clicked, this, [this]() {
        QColor c = QColorDialog::getColor(QColor(bgCol), this, "Фон кадра");
        if (c.isValid()) { bgCol = c.name(); bg->setStyleSheet("QPushButton{background:" + bgCol + ";color:" + (c.lightness() > 128 ? "#000" : "#fff") + ";}"); }
    });
    connect(apply, &QPushButton::clicked, this, [this]() {
        project->W = (w->value() / 2) * 2; project->H = (h->value() / 2) * 2;
        project->fps = fps->currentText().toInt();
        project->bgColor = bgCol;
        emit changed();
    });
    reload();
}

void ProjectPropsPanel::reload() {
    w->setValue(project->W); h->setValue(project->H);
    fps->setCurrentText(QString::number(project->fps));
    bgCol = project->bgColor;
    bg->setStyleSheet("QPushButton{background:" + bgCol + ";color:" + (QColor(bgCol).lightness() > 128 ? "#000" : "#fff") + ";}");
}

// ================= Структура проекта =================
StructurePanel::StructurePanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    QVBoxLayout* l = new QVBoxLayout(this);
    tree = new QTreeWidget; tree->setHeaderHidden(true);
    l->addWidget(tree);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        QVariant v = it->data(0, Qt::UserRole);
        if (v.isValid()) emit clipActivated(v.toInt());
    });
    refresh();
}

void StructurePanel::refresh() {
    QString s = project->tracks.join(",");
    for (const ClipPtr& c : project->clips) s += QString("|%1:%2:%3:%4").arg(c->id).arg(c->start, 0, 'f', 2).arg(c->dur, 0, 'f', 2).arg(c->track);
    if (s == sig) return;
    sig = s;
    tree->clear();
    for (const QString& t : project->tracks) {
        QList<ClipPtr> cl;
        for (const ClipPtr& c : project->clips) if (c->track == t) cl.append(c);
        std::sort(cl.begin(), cl.end(), [](const ClipPtr& a, const ClipPtr& b) { return a->start < b->start; });
        QTreeWidgetItem* top = new QTreeWidgetItem(QStringList(QString("%1 %2  (%3)").arg(isVideoTrack(t) ? "🎞" : "🎵", t).arg(cl.size())));
        tree->addTopLevelItem(top);
        for (const ClipPtr& c : cl) {
            QString name = c->kind == "text" ? c->text : QFileInfo(project->assets.value(c->assetId).path).fileName();
            QTreeWidgetItem* it = new QTreeWidgetItem(QStringList(QString("%1  %2 – %3").arg(name, fmtT(c->start), fmtT(c->end()))));
            it->setData(0, Qt::UserRole, c->id);
            top->addChild(it);
        }
    }
    tree->expandAll();
}

// ================= Заметки =================
NotesPanel::NotesPanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    QVBoxLayout* l = new QVBoxLayout(this);
    edit = new QPlainTextEdit;
    edit->setPlaceholderText("Заметки к проекту (сохраняются вместе с проектом)…");
    l->addWidget(edit);
    connect(edit, &QPlainTextEdit::textChanged, this, [this]() { project->notes = edit->toPlainText(); });
    reload();
}
void NotesPanel::reload() { edit->blockSignals(true); edit->setPlainText(project->notes); edit->blockSignals(false); }

// ================= Пресеты =================
PresetsPanel::PresetsPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    list = new QListWidget;
    QHBoxLayout* b = new QHBoxLayout;
    QPushButton* ap = new QPushButton("Применить к клипу"), *del = new QPushButton("Удалить"), *rf = new QPushButton("⟳");
    b->addWidget(ap); b->addWidget(del); b->addWidget(rf);
    l->addWidget(new QLabel("Твои пресеты эффектов (сохраняются из инспектора)"));
    l->addWidget(list, 1); l->addLayout(b);
    connect(ap, &QPushButton::clicked, this, [this]() { if (list->currentItem()) emit applyPreset(list->currentItem()->text()); });
    connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { emit applyPreset(it->text()); });
    connect(del, &QPushButton::clicked, this, [this]() { if (list->currentItem()) { deletePreset(list->currentItem()->text()); refresh(); } });
    connect(rf, &QPushButton::clicked, this, [this]() { refresh(); });
    refresh();
}
void PresetsPanel::refresh() { list->clear(); list->addItems(presetNames()); }

// ================= Запись =================
RecorderPanel::RecorderPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    QPushButton* v = new QPushButton("🎙 Записать голос на аудиодорожку");
    QPushButton* s = new QPushButton("🖥 Записать экран");
    QPushButton* st = new QPushButton("■ Остановить запись экрана");
    QComboBox* mic = new QComboBox;
    const QList<QAudioDevice> devs = QMediaDevices::audioInputs();
    for (const QAudioDevice& d : devs) mic->addItem(d.description(), d.id());
    int idx = mic->findData(AppSettings::get("mic_id").toByteArray());
    if (idx >= 0) mic->setCurrentIndex(idx);
    l->addWidget(new QLabel("Микрофон:")); l->addWidget(mic); l->addWidget(v); l->addWidget(s); l->addWidget(st); l->addStretch(1);
    connect(mic, QOverload<int>::of(&QComboBox::activated), this, [mic](int i) { AppSettings::set("mic_id", mic->itemData(i).toByteArray()); });
    connect(v, &QPushButton::clicked, this, &RecorderPanel::voiceRequested);
    connect(s, &QPushButton::clicked, this, &RecorderPanel::screenRequested);
    connect(st, &QPushButton::clicked, this, &RecorderPanel::stopScreenRequested);
}

// ================= Файловый браузер =================
FileBrowserPanel::FileBrowserPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    QFileSystemModel* model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());
    model->setNameFilters(QStringList() << "*.mp4" << "*.mov" << "*.mkv" << "*.avi" << "*.webm" << "*.m4v" << "*.mp3" << "*.wav" << "*.flac" << "*.ogg"
                                         << "*.m4a" << "*.aac" << "*.png" << "*.jpg" << "*.jpeg" << "*.webp" << "*.bmp" << "*.uvmvideos");
    model->setNameFilterDisables(false);
    QTreeView* tree = new QTreeView;
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::homePath()));
    for (int i = 1; i < 4; ++i) tree->hideColumn(i);
    tree->setHeaderHidden(true);
    tree->setDragEnabled(true);
    l->addWidget(new QLabel("Двойной клик — добавить на таймлайн; можно перетащить мышью"));
    l->addWidget(tree);
    connect(tree, &QTreeView::doubleClicked, this, [this, model](const QModelIndex& i) {
        if (!model->isDir(i)) emit fileActivated(model->filePath(i));
    });
}

// ================= Таймкод =================
TimecodePanel::TimecodePanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    label = new QLabel("00:00:00:00");
    label->setAlignment(Qt::AlignCenter);
    QFont f("monospace"); f.setPointSize(30); f.setBold(true);
    label->setFont(f);
    l->addWidget(label);
}
void TimecodePanel::setTime(double t, int fps) {
    int total = (int)t, h = total / 3600, m = (total / 60) % 60, s = total % 60, fr = (int)((t - total) * fps);
    label->setText(QString("%1:%2:%3:%4").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')).arg(fr, 2, 10, QChar('0')));
}

// ================= Горячие клавиши (шпаргалка) =================
HotkeysPanel::HotkeysPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({"Действие", "Клавиши"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l->addWidget(table);
}
void HotkeysPanel::refresh() {
    table->setRowCount(0);
    for (QAction* a : actions) {
        QStringList ks;
        for (const QKeySequence& k : a->shortcuts()) ks << k.toString(QKeySequence::NativeText);
        if (ks.isEmpty()) continue;
        int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(a->text().remove('&')));
        table->setItem(r, 1, new QTableWidgetItem(ks.join("   /   ")));
    }
    table->resizeColumnToContents(0);
}

// ================= Статистика и журнал =================
StatsPanel::StatsPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    label = new QLabel; label->setAlignment(Qt::AlignTop | Qt::AlignLeft); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(label);
}
void StatsPanel::setText(const QString& t) { label->setText(t); }

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    edit = new QPlainTextEdit; edit->setReadOnly(true); edit->setMaximumBlockCount(500);
    l->addWidget(edit);
}
void LogPanel::append(const QString& msg) { edit->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + msg); }

// ================= Scopes =================
void ScopesPanel::setImage(const QImage& img) {
    std::memset(hist, 0, sizeof(hist));
    QVector<int> acc(img.width() * 128, 0);
    vec = QImage(128, 128, QImage::Format_RGB32);
    vec.fill(QColor(8, 9, 11));
    for (int y = 0; y < img.height(); ++y) {
        const uchar* row = img.constScanLine(y);
        for (int x = 0; x < img.width(); ++x) {
            int r = row[x * 3], g = row[x * 3 + 1], b = row[x * 3 + 2];
            hist[0][r]++; hist[1][g]++; hist[2][b]++;
            int l = (r * 299 + g * 587 + b * 114) / 1000;
            acc[x * 128 + (127 - l * 127 / 255)]++;
            int u = 64 + (int)((b - l) * 0.5), v = 64 - (int)((r - l) * 0.5);
            if (u >= 0 && u < 128 && v >= 0 && v < 128) vec.setPixelColor(u, v, QColor(r, g, b));
        }
    }
    wave = QImage(img.width(), 128, QImage::Format_RGB32);
    wave.fill(QColor(8, 9, 11));
    for (int x = 0; x < img.width(); ++x)
        for (int y = 0; y < 128; ++y) { int a = acc[x * 128 + y]; if (a) { int v = qMin(60 + a * 40, 255); wave.setPixelColor(x, y, QColor(v / 2, v, v / 2)); } }
    update();
}

void ScopesPanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#101113"));
    const int W = width(), H = height(), cw = W / 3;
    // гистограмма
    int mx = 1;
    for (int c = 0; c < 3; ++c) for (int i = 0; i < 256; ++i) mx = qMax(mx, hist[c][i]);
    static const QColor cols[3] = {QColor(255, 80, 80, 160), QColor(80, 255, 80, 160), QColor(90, 120, 255, 160)};
    for (int c = 0; c < 3; ++c) {
        QPolygonF poly; poly << QPointF(4, H - 14);
        for (int i = 0; i < 256; ++i) poly << QPointF(4 + i * (cw - 8) / 255.0, H - 14 - std::sqrt((double)hist[c][i] / mx) * (H - 30));
        poly << QPointF(cw - 4, H - 14);
        p.setBrush(cols[c]); p.setPen(Qt::NoPen); p.drawPolygon(poly);
    }
    p.setPen(QColor("#6b7280")); p.drawText(6, 12, "Гистограмма RGB");
    if (!wave.isNull()) { p.drawImage(QRect(cw, 16, cw - 4, H - 20), wave); p.setPen(QColor("#6b7280")); p.drawText(cw + 6, 12, "Яркость (waveform)"); }
    if (!vec.isNull()) { int s = qMin(cw - 4, H - 20); p.drawImage(QRect(2 * cw + (cw - s) / 2, 16, s, s), vec); p.setPen(QColor("#6b7280")); p.drawText(2 * cw + 6, 12, "Вектороскоп"); }
}

// ================= Шрифты =================
FontsPanel::FontsPanel(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    picker = new FontPicker;
    sample = new QLineEdit("Hello Привет Γειά σου 你好 こんにちは");
    preview = new QLabel; preview->setMinimumHeight(110); preview->setStyleSheet("background:#17181b;"); preview->setAlignment(Qt::AlignCenter);
    QPushButton* ap = new QPushButton("Применить к выбранному тексту");
    l->addWidget(picker); l->addWidget(new QLabel("Пример текста:")); l->addWidget(sample); l->addWidget(preview, 1); l->addWidget(ap);
    connect(picker, &FontPicker::fontChosen, this, [this](QString f) { font = f; updatePreview(); });
    connect(sample, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });
    connect(ap, &QPushButton::clicked, this, [this]() { emit applyFont(font); });
    updatePreview();
}
void FontsPanel::updatePreview() {
    TextStyle s; s.text = sample->text(); s.font = font; s.size = 0.6; s.outline = 1;
    QImage img = renderTextImage(s, 1280, 720);
    preview->setPixmap(QPixmap::fromImage(img).scaled(preview->width() > 20 ? preview->width() - 10 : 300, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
