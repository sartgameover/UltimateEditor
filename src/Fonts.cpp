#include "Fonts.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QMessageBox>
#include <cmath>
#include <functional>
#include <QFileInfo>
#include <QPixmap>

// ================= хранение штриховых шрифтов =================
QString strokeFontDir() {
    QString d = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/fonts";
    QDir().mkpath(d);
    return d;
}

QStringList strokeFontNames() {
    QStringList r;
    for (const QFileInfo& fi : QDir(strokeFontDir()).entryInfoList(QStringList() << "*.uvfont", QDir::Files)) r << fi.completeBaseName();
    return r;
}

bool loadStrokeFont(const QString& name, StrokeFont& out) {
    QFile f(strokeFontDir() + "/" + name + ".uvfont");
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.isEmpty()) return false;
    out = StrokeFont();
    out.name = name;
    out.thickness = o["thickness"].toDouble(0.08);
    QJsonObject g = o["glyphs"].toObject();
    for (auto it = g.constBegin(); it != g.constEnd(); ++it) {
        QVector<QVector<QPointF>> strokes;
        for (const QJsonValue& sv : it.value().toArray()) {
            QVector<QPointF> pts;
            for (const QJsonValue& pv : sv.toArray()) { QJsonArray a = pv.toArray(); pts.append(QPointF(a.at(0).toDouble(), a.at(1).toDouble())); }
            strokes.append(pts);
        }
        out.glyphs[it.key().toInt()] = strokes;
    }
    return true;
}

bool saveStrokeFont(const StrokeFont& sf) {
    QJsonObject o, g;
    o["thickness"] = sf.thickness;
    for (auto it = sf.glyphs.constBegin(); it != sf.glyphs.constEnd(); ++it) {
        QJsonArray strokes;
        for (const QVector<QPointF>& s : it.value()) {
            QJsonArray pts;
            for (const QPointF& p : s) pts.append(QJsonArray{p.x(), p.y()});
            strokes.append(pts);
        }
        g[QString::number(it.key())] = strokes;
    }
    o["glyphs"] = g;
    QFile f(strokeFontDir() + "/" + sf.name + ".uvfont");
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return true;
}

// ================= отрисовка текста =================
QImage renderStrokeTextImage(const StrokeFont& sf, const TextStyle& s, int px) {
    const QStringList lines = (s.text.isEmpty() ? QString("Text") : s.text).split('\n');
    const double adv = px * 0.7, sp = px * 0.45, lineH = px * 1.25;
    const double pad = s.outline + px * 0.1 + 6;
    double maxW = 1;
    QVector<double> widths;
    for (const QString& l : lines) {
        double w = 0;
        for (QChar ch : l) w += (ch == ' ') ? sp : adv;
        widths.append(w); maxW = qMax(maxW, w);
    }
    int iw = (int)std::ceil(maxW + pad * 2), ih = (int)std::ceil(lines.size() * lineH + pad * 2);
    QImage img(qMax(iw, 16), qMax(ih, 16), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    auto drawAll = [&](const QColor& col, double extra, QPointF off) {
        QPen pen(col, sf.thickness * px + extra, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        for (int li = 0; li < lines.size(); ++li) {
            double x0 = pad + (s.align == 0 ? 0 : s.align == 2 ? maxW - widths[li] : (maxW - widths[li]) / 2) + off.x();
            double y0 = pad + li * lineH + off.y();
            double x = x0;
            for (QChar ch : lines[li]) {
                if (ch == ' ') { x += sp; continue; }
                auto g = sf.glyphs.constFind((int)ch.unicode());
                if (g != sf.glyphs.constEnd()) {
                    for (const QVector<QPointF>& st : g.value()) {
                        if (st.size() == 1) { p.drawPoint(QPointF(x + st[0].x() * adv, y0 + st[0].y() * px)); continue; }
                        QPainterPath path;
                        path.moveTo(x + st[0].x() * adv, y0 + st[0].y() * px);
                        for (int i = 1; i < st.size(); ++i) path.lineTo(x + st[i].x() * adv, y0 + st[i].y() * px);
                        p.drawPath(path);
                    }
                }
                x += adv;
            }
        }
    };
    if (s.shadow) drawAll(QColor(0, 0, 0, 150), 0, QPointF(px * 0.05 + 2, px * 0.05 + 2));
    if (s.outline > 0.01) drawAll(QColor(s.outlineColor), s.outline * 2, QPointF(0, 0));
    drawAll(QColor(s.color), 0, QPointF(0, 0));
    p.end();
    return img.convertToFormat(QImage::Format_RGBA8888);
}

QImage renderTextImage(const TextStyle& s, int W, int H) {
    const int px = qMax((int)(H / 12.0 * s.size), 8);
    if (s.font.startsWith("UVF:")) {
        StrokeFont sf;
        if (loadStrokeFont(s.font.mid(4), sf)) return renderStrokeTextImage(sf, s, px);
    }
    const QString text = s.text.isEmpty() ? QString("Text") : s.text;
    const double pad = s.outline + (s.shadow ? px * 0.08 + 3 : 0) + 6;
    QFont f(s.font.isEmpty() || s.font.startsWith("UVF:") ? QString("Sans Serif") : s.font);
    f.setPixelSize(px); f.setBold(s.bold); f.setItalic(s.italic);
    const int fl = Qt::TextWordWrap | (s.align == 0 ? Qt::AlignLeft : s.align == 2 ? Qt::AlignRight : Qt::AlignHCenter);
    QFontMetricsF fm(f);
    QRectF br = fm.boundingRect(QRectF(0, 0, W * 0.9, 100000), fl, text);
    int iw = qMax((int)std::ceil(br.width() + pad * 2), 16), ih = qMax((int)std::ceil(br.height() + pad * 2), 16);
    QImage img(iw, ih, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(f);
    QRectF r(pad, pad, br.width(), br.height());
    if (s.shadow) { p.setPen(QColor(0, 0, 0, 150)); p.drawText(r.translated(px * 0.05 + 2, px * 0.05 + 2), fl, text); }
    if (s.outline > 0.01) {
        p.setPen(QColor(s.outlineColor));
        for (int k = 0; k < 16; ++k) {
            double a = k * 3.14159265 / 8.0;
            p.drawText(r.translated(std::cos(a) * s.outline, std::sin(a) * s.outline), fl, text);
        }
    }
    p.setPen(QColor(s.color));
    p.drawText(r, fl, text);
    p.end();
    return img.convertToFormat(QImage::Format_RGBA8888);
}

// ================= выбор шрифта =================
FontPicker::FontPicker(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    writing = new QComboBox;
    writing->addItem("Все письменности", (int)QFontDatabase::Any);
    for (int i = 1; i < (int)QFontDatabase::WritingSystemsCount; ++i) {
        QFontDatabase::WritingSystem ws = (QFontDatabase::WritingSystem)i;
        writing->addItem(QFontDatabase::writingSystemName(ws), i);
    }
    fonts = new QComboBox;
    fonts->setMaxVisibleItems(24);
    QPushButton* file = new QPushButton("📂 Шрифт из файла…");
    QPushButton* make = new QPushButton("✍ Создать свой шрифт…");
    l->addWidget(writing); l->addWidget(fonts); l->addWidget(file); l->addWidget(make);
    refreshFonts();
    connect(writing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { refreshFonts(); });
    connect(fonts, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (updating) return;
        cur = fonts->itemData(i).toString();
        emit fontChosen(cur);
    });
    connect(file, &QPushButton::clicked, this, [this]() {
        QStringList fs = QFileDialog::getOpenFileNames(this, "Файл шрифта", QString(), "Шрифты (*.ttf *.otf *.ttc *.woff *.woff2)");
        for (const QString& f : fs) QFontDatabase::addApplicationFont(f);
        refreshFonts();
    });
    connect(make, &QPushButton::clicked, this, [this]() { FontCreatorDialog d(this); d.exec(); refreshFonts(); });
}

void FontPicker::refreshFonts() {
    updating = true;
    fonts->clear();
    fonts->addItem("(по умолчанию)", QString());
    for (const QString& n : strokeFontNames()) fonts->addItem("✍ " + n, "UVF:" + n);
    QFontDatabase::WritingSystem ws = (QFontDatabase::WritingSystem)writing->currentData().toInt();
    for (const QString& fam : QFontDatabase::families(ws)) {
        fonts->addItem(fam, fam);
        fonts->setItemData(fonts->count() - 1, QFont(fam), Qt::FontRole);          // каждое название — своим шрифтом
    }
    int idx = fonts->findData(cur);
    fonts->setCurrentIndex(idx >= 0 ? idx : 0);
    updating = false;
}

void FontPicker::setCurrent(const QString& font) {
    cur = font;
    updating = true;
    int idx = fonts->findData(font);
    if (idx < 0 && !font.isEmpty() && !font.startsWith("UVF:")) { fonts->addItem(font, font); idx = fonts->count() - 1; }
    fonts->setCurrentIndex(idx >= 0 ? idx : 0);
    updating = false;
}

// ================= редактор шрифта =================
class GlyphCanvas : public QWidget {
public:
    QVector<QVector<QPointF>>* strokes = nullptr;
    double thickness = 0.08;
    std::function<void()> onChange;
    GlyphCanvas() { setFixedSize(340, 340); setCursor(Qt::CrossCursor); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#17181b"));
        p.setPen(QPen(QColor("#2e3036"), 1));
        for (int i = 1; i < 10; ++i) { p.drawLine(QPointF(i * width() / 10.0, 0), QPointF(i * width() / 10.0, height())); p.drawLine(QPointF(0, i * height() / 10.0), QPointF(width(), i * height() / 10.0)); }
        p.setPen(QPen(QColor("#4dabf7"), 1, Qt::DashLine));
        for (double y : {0.2, 0.45, 0.8}) p.drawLine(QPointF(0, y * height()), QPointF(width(), y * height()));
        p.setPen(QColor("#6b7280"));
        p.drawText(4, (int)(0.2 * height()) - 3, "верх заглавных");
        p.drawText(4, (int)(0.45 * height()) - 3, "верх строчных");
        p.drawText(4, (int)(0.8 * height()) - 3, "базовая линия");
        if (!strokes) return;
        QPen pen(QColor("#ffffff"), thickness * width() * 0.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        for (const QVector<QPointF>& st : *strokes) {
            if (st.size() == 1) { p.drawPoint(QPointF(st[0].x() * width(), st[0].y() * height())); continue; }
            for (int i = 1; i < st.size(); ++i)
                p.drawLine(QPointF(st[i - 1].x() * width(), st[i - 1].y() * height()), QPointF(st[i].x() * width(), st[i].y() * height()));
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (!strokes) return;
        if (e->button() == Qt::RightButton) { if (!strokes->isEmpty()) strokes->removeLast(); update(); if (onChange) onChange(); return; }
        QVector<QPointF> st;
        st.append(QPointF(e->position().x() / width(), e->position().y() / height()));
        strokes->append(st);
        drawing = true; update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!strokes || !drawing || strokes->isEmpty()) return;
        QPointF pt(qBound(0.0, e->position().x() / width(), 1.0), qBound(0.0, e->position().y() / height(), 1.0));
        QVector<QPointF>& st = strokes->last();
        QPointF d = pt - st.last();
        if (std::hypot(d.x(), d.y()) > 0.012) { st.append(pt); update(); }
    }
    void mouseReleaseEvent(QMouseEvent*) override { drawing = false; if (onChange) onChange(); }
private:
    bool drawing = false;
};

FontCreatorDialog::FontCreatorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Создание своего шрифта");
    resize(900, 640);
    QVBoxLayout* root = new QVBoxLayout(this);
    QHBoxLayout* top = new QHBoxLayout;
    nameEdit = new QLineEdit("МойШрифт"); nameEdit->setPlaceholderText("Название шрифта");
    existing = new QComboBox; existing->addItem("— новый —"); existing->addItems(strokeFontNames());
    thick = new QSlider(Qt::Horizontal); thick->setRange(2, 24); thick->setValue(8); thick->setMaximumWidth(200);
    top->addWidget(new QLabel("Название:")); top->addWidget(nameEdit, 1);
    top->addWidget(new QLabel("Править:")); top->addWidget(existing);
    top->addWidget(new QLabel("Толщина:")); top->addWidget(thick);
    root->addLayout(top);
    QHBoxLayout* mid = new QHBoxLayout;
    QVBoxLayout* left = new QVBoxLayout;
    group = new QComboBox;
    group->addItems({"Латиница A–Z", "Латиница a–z", "Цифры", "Знаки", "Кириллица (заглавные)", "Кириллица (строчные)", "Греческий (заглавные)", "Греческий (строчные)"});
    chars = new QListWidget;
    chars->setViewMode(QListView::IconMode);
    chars->setResizeMode(QListView::Adjust);
    chars->setGridSize(QSize(46, 46));
    chars->setMovement(QListView::Static);
    QFont cf = chars->font(); cf.setPointSize(16); chars->setFont(cf);
    left->addWidget(group); left->addWidget(chars, 1);
    QWidget* lw = new QWidget; lw->setLayout(left); lw->setFixedWidth(300);
    mid->addWidget(lw);
    QVBoxLayout* right = new QVBoxLayout;
    canvas = new GlyphCanvas;
    right->addWidget(canvas);
    right->addWidget(new QLabel("Рисуй мышью. ПКМ — убрать последний штрих."));
    QHBoxLayout* btns = new QHBoxLayout;
    QPushButton* clr = new QPushButton("Очистить символ"), *save = new QPushButton("💾 Сохранить шрифт");
    btns->addWidget(clr); btns->addWidget(save);
    right->addLayout(btns);
    right->addStretch(1);
    mid->addLayout(right);
    root->addLayout(mid, 1);
    sample = new QLineEdit("Hello Привіт Γειά 123");
    preview = new QLabel; preview->setMinimumHeight(110); preview->setStyleSheet("background:#17181b;");
    root->addWidget(new QLabel("Предпросмотр:")); root->addWidget(sample); root->addWidget(preview);

    canvas->onChange = [this]() { updatePreview(); };
    fillChars();
    connect(group, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { fillChars(); });
    connect(chars, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* it, QListWidgetItem*) { if (it) selectChar(it->data(Qt::UserRole).toInt()); });
    connect(thick, &QSlider::valueChanged, this, [this](int v) { font.thickness = v / 100.0; canvas->thickness = font.thickness; canvas->update(); updatePreview(); });
    connect(clr, &QPushButton::clicked, this, [this]() { font.glyphs[curChar].clear(); canvas->update(); updatePreview(); });
    connect(sample, &QLineEdit::textChanged, this, [this](const QString&) { updatePreview(); });
    connect(existing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (i <= 0) { font = StrokeFont(); nameEdit->setText("МойШрифт"); }
        else if (loadStrokeFont(existing->currentText(), font)) { nameEdit->setText(font.name); thick->setValue((int)std::lround(font.thickness * 100)); }
        selectChar(curChar);
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        QString n = nameEdit->text().trimmed();
        if (n.isEmpty()) { QMessageBox::warning(this, "Шрифт", "Введи название шрифта"); return; }
        font.name = n;
        if (saveStrokeFont(font)) QMessageBox::information(this, "Шрифт", "Шрифт «" + n + "» сохранён. Он появится в списке шрифтов (✍).");
        else QMessageBox::warning(this, "Шрифт", "Не удалось сохранить");
    });
    selectChar('A');
}

void FontCreatorDialog::fillChars() {
    static const QStringList sets = {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789", ".,:;!?-+=()[]{}'\"/\\@#%&*_<>$",
        QString::fromUtf8("АБВГҐДЕЁЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"), QString::fromUtf8("абвгґдеёєжзиіїйклмнопрстуфхцчшщъыьэюя"),
        QString::fromUtf8("ΑΒΓΔΕΖΗΘΙΚΛΜΝΞΟΠΡΣΤΥΦΧΨΩ"), QString::fromUtf8("αβγδεζηθικλμνξοπρςστυφχψω")};
    chars->blockSignals(true);
    chars->clear();
    for (QChar ch : sets[group->currentIndex()]) {
        QListWidgetItem* it = new QListWidgetItem(QString(ch));
        it->setData(Qt::UserRole, (int)ch.unicode());
        it->setTextAlignment(Qt::AlignCenter);
        chars->addItem(it);
    }
    chars->blockSignals(false);
    if (chars->count()) chars->setCurrentRow(0);
}

void FontCreatorDialog::selectChar(int u) {
    curChar = u;
    canvas->strokes = &font.glyphs[u];
    canvas->thickness = font.thickness;
    canvas->update();
    updatePreview();
}

void FontCreatorDialog::updatePreview() {
    TextStyle s; s.text = sample->text(); s.color = "#ffffff"; s.outline = 0; s.size = 1.0;
    QImage img = renderStrokeTextImage(font, s, 70);
    preview->setPixmap(QPixmap::fromImage(img).scaledToHeight(qMin(preview->height() - 8, img.height()), Qt::SmoothTransformation));
}
