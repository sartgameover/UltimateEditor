#include <QPainterPath>
#include <QDragMoveEvent>
#include "Panels.h"
#include "Effects.h"
#include "AudioFx.h"
#include <QColorDialog>
#include <QDial>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QGridLayout>
#include <QFontDatabase>
#include <functional>
#include "Fonts.h"
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QInputDialog>
#include <QFrame>
#include <QLineEdit>
#include <algorithm>
#include <cmath>

// ================= Assets =================
AssetsPanel::AssetsPanel(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { emit assetActivated(it->data(Qt::UserRole).toInt()); });
}

void AssetsPanel::addAsset(const Asset& a) {
    for (int i = 0; i < count(); ++i) if (item(i)->data(Qt::UserRole).toInt() == a.id) return;
    QString icon = a.kind == "video" ? "🎞" : a.kind == "audio" ? "🎵" : "🖼";
    QListWidgetItem* it = new QListWidgetItem(QString("%1 %2  (%3 с)").arg(icon, QFileInfo(a.path).fileName()).arg(a.duration, 0, 'f', 1));
    it->setData(Qt::UserRole, a.id);
    it->setData(Qt::UserRole + 1, a.path);
    it->setToolTip(a.path);
    addItem(it);
}

QMimeData* AssetsPanel::mimeData(const QList<QListWidgetItem*>& items) const {
    QMimeData* m = new QMimeData;
    QList<QUrl> urls;
    for (QListWidgetItem* it : items) urls << QUrl::fromLocalFile(it->data(Qt::UserRole + 1).toString());
    m->setUrls(urls);
    return m;
}
void AssetsPanel::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void AssetsPanel::dragMoveEvent(QDragMoveEvent* e) { e->acceptProposedAction(); }
void AssetsPanel::dropEvent(QDropEvent* e) {
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && !mediaKind(u.toLocalFile()).isEmpty()) paths << u.toLocalFile();
    if (!paths.isEmpty()) { emit filesAdded(paths); e->acceptProposedAction(); }
}

// ================= Effects =================
EffectsTree::EffectsTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setMouseTracking(true);
    setDragEnabled(true);                                       // эффект можно перетащить прямо на клип
    setDragDropMode(QAbstractItemView::DragOnly);
    connect(this, &QTreeWidget::itemEntered, this, [this](QTreeWidgetItem* it, int) {
        QString kind = it->data(0, Qt::UserRole).toString();
        emit hovered(kind == "fx" ? it->text(0) : QString());
    });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        QString kind = it->data(0, Qt::UserRole).toString();
        if (kind == "fx" || kind == "afx") emit applyEffect(it->text(0));
        else if (kind == "preset") emit applyPreset(it->text(0));
    });
    rebuild();
}

void EffectsTree::rebuild() {
    clear();
    QMap<QString, QTreeWidgetItem*> cats;
    auto category = [&](const QString& name) {
        if (!cats.contains(name)) {
            QTreeWidgetItem* c = new QTreeWidgetItem(QStringList(name));
            c->setFlags(c->flags() & ~Qt::ItemIsDragEnabled);
            addTopLevelItem(c);
            cats[name] = c;
        }
        return cats[name];
    };
    if (filter != "afx") for (const EffectDef& d : effectDefs()) {
        QTreeWidgetItem* it = new QTreeWidgetItem(QStringList(d.name));
        it->setData(0, Qt::UserRole, "fx");
        category(d.category)->addChild(it);
    }
    if (filter != "fx") for (const AudioFxDef& d : audioFxDefs()) {
        QTreeWidgetItem* it = new QTreeWidgetItem(QStringList(d.name));
        it->setData(0, Qt::UserRole, "afx");
        category(d.category)->addChild(it);
    }
    QTreeWidgetItem* top = category("★ Мои пресеты");
    for (const QString& n : presetNames()) {
        QTreeWidgetItem* it = new QTreeWidgetItem(QStringList(n));
        it->setData(0, Qt::UserRole, "preset");
        top->addChild(it);
    }
    sortItems(0, Qt::AscendingOrder);
    expandAll();
}

QMimeData* EffectsTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    QMimeData* m = new QMimeData;
    if (!items.isEmpty()) {
        QString k = items[0]->data(0, Qt::UserRole).toString();
        if (k == "fx" || k == "afx" || k == "preset") m->setData("application/x-uve-effect", (k + ":" + items[0]->text(0)).toUtf8());
    }
    return m;
}

void EffectsTree::leaveEvent(QEvent* e) { emit hovered(QString()); QTreeWidget::leaveEvent(e); }

// ================= Animations =================
AnimationsList::AnimationsList(QWidget* parent) : QListWidget(parent) {
    auto section = [this](const QString& title, const QStringList& names) {
        QListWidgetItem* h = new QListWidgetItem(title);
        h->setFlags(Qt::NoItemFlags);
        QFont f = h->font(); f.setBold(true); h->setFont(f);
        addItem(h);
        for (const QString& n : names) addItem(n);
    };
    section("— ВХОД (длительность тянется голубым кружком) —", animationInNames());
    section("— ВЫХОД —", animationOutNames());
    section("— ПОСТОЯННЫЕ —", animationLoopNames());
    setDragEnabled(true);                                       // можно перетащить анимацию на клип
    setDragDropMode(QAbstractItemView::DragOnly);
    setToolTip("Двойной клик или перетаскивание на клип. Плашка с названием появится на клипе, ✕ убирает её.");
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (it->flags() != Qt::NoItemFlags) emit applyAnimation(it->text());
    });
}

QMimeData* AnimationsList::mimeData(const QList<QListWidgetItem*>& items) const {
    QMimeData* m = new QMimeData;
    if (!items.isEmpty() && items[0]->flags() != Qt::NoItemFlags) m->setData("application/x-uve-effect", ("anim:" + items[0]->text()).toUtf8());
    return m;
}

// ================= BezierEditor =================
namespace {
struct CurvePreset { const char* name; double a, b, c, d; };
const CurvePreset kCurves[] = {
    {"Линейная", 0, 0, 1, 1}, {"Ease (плавно)", 0.25, 0.1, 0.25, 1}, {"Ease In (разгон)", 0.42, 0, 1, 1}, {"Ease Out (торможение)", 0, 0, 0.58, 1},
    {"Ease In-Out", 0.42, 0, 0.58, 1}, {"Quad In", 0.55, 0.085, 0.68, 0.53}, {"Quad Out", 0.25, 0.46, 0.45, 0.94},
    {"Cubic In-Out", 0.645, 0.045, 0.355, 1}, {"Circ Out", 0.075, 0.82, 0.165, 1}, {"Back In (откат)", 0.6, -0.28, 0.735, 0.045},
    {"Back Out (перелёт)", 0.175, 0.885, 0.32, 1.275}, {"Back In-Out", 0.68, -0.55, 0.265, 1.55}, {"Резко (Sharp)", 0.4, 0, 0.2, 1},
    {"Щелчок (Snap)", 0.9, 0, 0.1, 1}, {"Своя", 0.25, 0.25, 0.75, 0.75}};
const int kCustomIdx = (int)(sizeof(kCurves) / sizeof(kCurves[0])) - 1;
}

class BezierCanvas : public QWidget {
public:
    double c[4] = {0.42, 0, 0.58, 1};
    std::function<void()> onBegin, onChange;
    BezierCanvas() { setMinimumSize(190, 160); }
protected:
    QRectF area() const { return QRectF(rect()).adjusted(12, 10, -12, -10); }
    QPointF toPt(double x, double y) const { QRectF a = area(); return QPointF(a.left() + x * a.width(), a.bottom() - (y + 0.5) / 2.0 * a.height()); }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#17181b"));
        p.setPen(QColor("#3a3d42"));
        p.drawRect(QRectF(toPt(0, 1), toPt(1, 0)));
        p.setPen(QPen(QColor("#4b5058"), 1, Qt::DashLine));
        p.drawLine(toPt(0, 0), toPt(1, 1));
        p.setPen(QPen(QColor("#6b7280"), 1));
        p.drawLine(toPt(0, 0), toPt(c[0], c[1]));
        p.drawLine(toPt(1, 1), toPt(c[2], c[3]));
        QPainterPath path;
        path.moveTo(toPt(0, 0));
        for (int i = 1; i <= 60; ++i) {
            double t = i / 60.0, s = 1 - t;
            double x = 3 * s * s * t * c[0] + 3 * s * t * t * c[2] + t * t * t;
            double y = 3 * s * s * t * c[1] + 3 * s * t * t * c[3] + t * t * t;
            path.lineTo(toPt(x, y));
        }
        p.setPen(QPen(QColor("#4dabf7"), 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setPen(QPen(QColor("#202124"), 1));
        p.setBrush(QColor("#ffd54a")); p.drawEllipse(toPt(c[0], c[1]), 6, 6);
        p.setBrush(QColor("#ff8a5c")); p.drawEllipse(toPt(c[2], c[3]), 6, 6);
        p.setBrush(QColor("#9aa0a6")); p.drawEllipse(toPt(0, 0), 3, 3); p.drawEllipse(toPt(1, 1), 3, 3);
    }
    void mousePressEvent(QMouseEvent* e) override {
        for (int h = 0; h < 2; ++h) {
            QPointF d = toPt(c[h * 2], c[h * 2 + 1]) - e->position();
            if (std::abs(d.x()) + std::abs(d.y()) < 16) { drag = h; if (onBegin) onBegin(); return; }
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (drag < 0) return;
        QRectF a = area();
        c[drag * 2] = qBound(0.0, (e->position().x() - a.left()) / a.width(), 1.0);
        c[drag * 2 + 1] = qBound(-0.5, (a.bottom() - e->position().y()) / a.height() * 2.0 - 0.5, 1.5);
        update();
        if (onChange) onChange();
    }
    void mouseReleaseEvent(QMouseEvent*) override { drag = -1; }
private:
    int drag = -1;
};

BezierEditor::BezierEditor(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    presets = new QComboBox;
    for (const CurvePreset& c : kCurves) presets->addItem(c.name);
    BezierCanvas* bc = new BezierCanvas;
    canvas = bc;
    info = new QLabel;
    info->setAlignment(Qt::AlignCenter);
    l->addWidget(presets); l->addWidget(bc); l->addWidget(info);
    bc->onBegin = [this]() { emit aboutToChange(); };
    bc->onChange = [this, bc]() {
        presets->blockSignals(true); presets->setCurrentIndex(kCustomIdx); presets->blockSignals(false);
        refreshInfo(bc->c[0], bc->c[1], bc->c[2], bc->c[3]);
        emit curveChanged(bc->c[0], bc->c[1], bc->c[2], bc->c[3]);
    };
    connect(presets, QOverload<int>::of(&QComboBox::activated), this, [this, bc](int i) {
        if (i == kCustomIdx) return;
        emit aboutToChange();
        bc->c[0] = kCurves[i].a; bc->c[1] = kCurves[i].b; bc->c[2] = kCurves[i].c; bc->c[3] = kCurves[i].d;
        bc->update();
        refreshInfo(bc->c[0], bc->c[1], bc->c[2], bc->c[3]);
        emit curveChanged(bc->c[0], bc->c[1], bc->c[2], bc->c[3]);
    });
    refreshInfo(bc->c[0], bc->c[1], bc->c[2], bc->c[3]);
}

void BezierEditor::refreshInfo(double a, double b, double c, double d) {
    info->setText(QString("cubic-bezier(%1, %2, %3, %4)").arg(a, 0, 'f', 2).arg(b, 0, 'f', 2).arg(c, 0, 'f', 2).arg(d, 0, 'f', 2));
}

void BezierEditor::setCurve(double a, double b, double c, double d) {
    BezierCanvas* bc = static_cast<BezierCanvas*>(canvas);
    bc->c[0] = a; bc->c[1] = b; bc->c[2] = c; bc->c[3] = d;
    bc->update();
    int idx = kCustomIdx;
    for (int i = 0; i < kCustomIdx; ++i)
        if (std::abs(kCurves[i].a - a) < 0.005 && std::abs(kCurves[i].b - b) < 0.005 && std::abs(kCurves[i].c - c) < 0.005 && std::abs(kCurves[i].d - d) < 0.005) idx = i;
    presets->blockSignals(true); presets->setCurrentIndex(idx); presets->blockSignals(false);
    refreshInfo(a, b, c, d);
}

// ================= KeyframeGraph =================
KeyframeGraph::KeyframeGraph(QWidget* parent) : QWidget(parent) { setMinimumHeight(140); }

void KeyframeGraph::setTarget(const ClipPtr& c, int idx, const QString& k, const QString& l, double a, double b) {
    clip = c; fxIndex = idx; key = k; label = l; lo = a; hi = b; dragIdx = -1; selIdx = -1; update();
    emit keySelected();
}
void KeyframeGraph::clearTarget() { clip.reset(); key.clear(); selIdx = -1; update(); emit keySelected(); }

Keyframe* KeyframeGraph::selectedKey() {
    KeyList* kl = list();
    if (!kl || selIdx < 0 || selIdx >= kl->size()) return nullptr;
    return &(*kl)[selIdx];
}

KeyList* KeyframeGraph::list() const {
    if (!clip || key.isEmpty()) return nullptr;
    if (fxIndex < 0) return &clip->keys[key];
    if (fxIndex >= (int)clip->effects.size()) return nullptr;
    return &clip->effects[fxIndex].keys[key];
}
QPointF KeyframeGraph::toPt(const Keyframe& k) const {
    QRectF r = QRectF(rect()).adjusted(8, 22, -8, -8);
    return QPointF(r.left() + k.t / qMax(clip->dur, 0.1) * r.width(), r.bottom() - (k.v - lo) / qMax(hi - lo, 1e-9) * r.height());
}
void KeyframeGraph::fromPt(const QPointF& p, double& t, double& v) const {
    QRectF r = QRectF(rect()).adjusted(8, 22, -8, -8);
    t = qBound(0.0, (p.x() - r.left()) / r.width(), 1.0) * clip->dur;
    v = lo + (r.bottom() - p.y()) / r.height() * (hi - lo);
}

void KeyframeGraph::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#1a1b1e"));
    p.setPen(QColor("#9aa0a6"));
    p.drawText(10, 16, "График: " + (label.isEmpty() ? QString("выберите параметр (кнопка 📈)") : label) + "   [2×клик — ключ, ПКМ — режим, клик — выбрать]");
    KeyList* kl = list();
    if (!kl || !clip) return;
    p.setPen(QPen(QColor("#4dabf7"), 2));
    QPointF prev; bool has = false;
    for (int i = 0; i <= 100; ++i) {
        double t = clip->dur * i / 100.0;
        double base = fxIndex < 0 ? clip->props.value(key, defaultProp(key)) : 0.0;
        Keyframe k; k.t = t; k.v = kl->isEmpty() ? base : evalKeys(*kl, t, base);
        QPointF pt = toPt(k);
        if (has && !kl->isEmpty()) p.drawLine(prev, pt);
        prev = pt; has = true;
    }
    for (int ki = 0; ki < kl->size(); ++ki) {
        const Keyframe k = (*kl)[ki];
        p.setBrush(k.mode == 2 ? QColor("#e5484d") : k.mode == 1 ? QColor("#46a758") : k.mode == 3 ? QColor("#b180ff") : QColor("#ffd54a"));
        p.setPen(ki == selIdx ? QPen(Qt::white, 2) : QPen(Qt::black));
        p.drawEllipse(toPt(k), 5, 5);
    }
}

void KeyframeGraph::mousePressEvent(QMouseEvent* e) {
    KeyList* kl = list();
    if (!kl) return;
    for (int i = 0; i < kl->size(); ++i) {
        QPointF d = toPt((*kl)[i]) - e->position();
        if (std::abs(d.x()) + std::abs(d.y()) < 12) {
            emit aboutToEdit();
            selIdx = i;
            if (e->button() == Qt::RightButton) { (*kl)[i].mode = ((*kl)[i].mode + 1) % 4; update(); emit edited(); }
            else dragIdx = i;
            emit keySelected();
            return;
        }
    }
}
void KeyframeGraph::mouseMoveEvent(QMouseEvent* e) {
    KeyList* kl = list();
    if (!kl || dragIdx < 0 || dragIdx >= kl->size()) return;
    double t, v; fromPt(e->position(), t, v);
    (*kl)[dragIdx].t = t; (*kl)[dragIdx].v = v;
    update(); emit edited();
}
void KeyframeGraph::mouseReleaseEvent(QMouseEvent*) {
    KeyList* kl = list();
    if (kl && dragIdx >= 0 && dragIdx < kl->size()) {
        double dt = (*kl)[dragIdx].t;
        std::sort(kl->begin(), kl->end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
        for (int i = 0; i < kl->size(); ++i) if (std::fabs((*kl)[i].t - dt) < 1e-9) selIdx = i;
        emit keySelected();
    }
    dragIdx = -1;
}
void KeyframeGraph::mouseDoubleClickEvent(QMouseEvent* e) {
    KeyList* kl = list();
    if (!kl) return;
    emit aboutToEdit();
    double t, v; fromPt(e->position(), t, v);
    setKey(*kl, t, v, 0);
    update(); emit edited();
}

// ================= Inspector =================
Inspector::Inspector(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    body = new QWidget;
    form = new QVBoxLayout(body);
    form->setAlignment(Qt::AlignTop);
    QScrollArea* sc = new QScrollArea;
    sc->setWidgetResizable(true);
    sc->setWidget(body);
    graph = new KeyframeGraph;
    connect(graph, &KeyframeGraph::edited, this, [this]() { emit changed(); });
    connect(graph, &KeyframeGraph::aboutToEdit, this, [this]() { emit aboutToChange("graph"); });
    QPushButton* pb = new QPushButton("★ Сохранить эффекты клипа как пресет");
    connect(pb, &QPushButton::clicked, this, [this]() {
        if (!clip || clip->effects.empty()) return;
        bool ok = false;
        QString n = QInputDialog::getText(this, "Пресет", "Название пресета:", QLineEdit::Normal, "", &ok);
        if (ok && !n.isEmpty()) { savePreset(n, clip->effects); emit presetSaved(); }
    });
    keyBox = new QWidget;
    QVBoxLayout* kb = new QVBoxLayout(keyBox);
    kb->setContentsMargins(0, 0, 0, 0);
    keyMode = new QComboBox;
    keyMode->addItems({"Линейная", "Плавная", "Ступенька (hold)", "Кривая Безье"});
    keyCurve = new BezierEditor;
    QHBoxLayout* kh = new QHBoxLayout;
    kh->addWidget(new QLabel("Выбранный ключ:")); kh->addWidget(keyMode, 1);
    kb->addLayout(kh); kb->addWidget(keyCurve);
    keyBox->setVisible(false);
    connect(graph, &KeyframeGraph::keySelected, this, [this]() { syncKeyEditor(); });
    connect(keyMode, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        Keyframe* k = graph->selectedKey();
        if (!k) return;
        emit aboutToChange("graph");
        k->mode = i;
        graph->update(); emit changed();
    });
    connect(keyCurve, &BezierEditor::aboutToChange, this, [this]() { emit aboutToChange("graph"); });
    connect(keyCurve, &BezierEditor::curveChanged, this, [this](double a, double b, double c, double d) {
        Keyframe* k = graph->selectedKey();
        if (!k) return;
        k->mode = 3; k->c1x = a; k->c1y = b; k->c2x = c; k->c2y = d;
        keyMode->blockSignals(true); keyMode->setCurrentIndex(3); keyMode->blockSignals(false);
        graph->update(); emit changed();
    });
    lay->addWidget(sc, 1);
    lay->addWidget(graph);
    lay->addWidget(keyBox);
    lay->addWidget(pb);
    rebuild();
}

void Inspector::syncKeyEditor() {
    Keyframe* k = graph->selectedKey();
    keyBox->setVisible(k != nullptr);
    if (!k) return;
    keyMode->blockSignals(true); keyMode->setCurrentIndex(qBound(0, k->mode, 3)); keyMode->blockSignals(false);
    keyCurve->setCurve(k->c1x, k->c1y, k->c2x, k->c2y);
}

QMap<QString, double>& Inspector::storeOf(int i) { return i < 0 ? clip->props : clip->effects[i].params; }
QMap<QString, KeyList>& Inspector::keysOf(int i) { return i < 0 ? clip->keys : clip->effects[i].keys; }

void Inspector::setClip(const ClipPtr& c) { clip = c; graph->clearTarget(); rebuild(); }
void Inspector::setNow(double t) { now = t; refreshValues(); }

void Inspector::refreshValues() {
    if (!clip) return;
    double local = qMax(now - clip->start, 0.0);
    for (Row& r : rows) {
        r.sp->blockSignals(true);
        r.sp->setValue(r.eval(local));
        r.sp->blockSignals(false);
    }
}

QWidget* Inspector::makeRow(const QString& label, int fxIdx, const QString& key, double lo, double hi, double def) {
    QWidget* w = new QWidget;
    QHBoxLayout* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(new QLabel(label), 1);
    QDoubleSpinBox* sp = new QDoubleSpinBox;
    sp->setRange(lo, hi); sp->setDecimals(3); sp->setSingleStep((hi - lo) / 100.0);
    ClipPtr c = clip;
    auto eval = [this, c, fxIdx, key, def](double local) -> double {
        if (fxIdx >= (int)c->effects.size()) return def;
        const QMap<QString, double>& st = fxIdx < 0 ? c->props : c->effects[fxIdx].params;
        const QMap<QString, KeyList>& ks = fxIdx < 0 ? c->keys : c->effects[fxIdx].keys;
        double base = st.value(key, def);
        auto it = ks.constFind(key);
        return (it != ks.constEnd() && !it.value().isEmpty()) ? evalKeys(it.value(), local, base) : base;
    };
    sp->setValue(eval(qMax(now - clip->start, 0.0)));
    connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, c, fxIdx, key](double v) {
        if (fxIdx >= (int)c->effects.size()) return;
        emit aboutToChange("insp");
        double local = qMax(now - c->start, 0.0);
        KeyList& kl = (fxIdx < 0 ? c->keys : c->effects[fxIdx].keys)[key];
        if (!kl.isEmpty()) setKey(kl, local, v, 0);
        else (fxIdx < 0 ? c->props : c->effects[fxIdx].params)[key] = v;
        emit changed();
    });
    QToolButton* kb = new QToolButton; kb->setText("◆"); kb->setToolTip("Ключевой кадр в текущее время");
    connect(kb, &QToolButton::clicked, this, [this, c, fxIdx, key, sp, label, lo, hi]() {
        if (fxIdx >= (int)c->effects.size()) return;
        emit aboutToChange(QString());
        double local = qMax(now - c->start, 0.0);
        setKey((fxIdx < 0 ? c->keys : c->effects[fxIdx].keys)[key], local, sp->value(), 0);
        graph->setTarget(c, fxIdx, key, label, lo, hi);
        emit changed();
    });
    QToolButton* gb = new QToolButton; gb->setText("📈"); gb->setToolTip("Показать график ключей");
    connect(gb, &QToolButton::clicked, this, [this, c, fxIdx, key, label, lo, hi]() { graph->setTarget(c, fxIdx, key, label, lo, hi); });
    h->addWidget(sp); h->addWidget(kb); h->addWidget(gb);
    Row row; row.sp = sp; row.eval = eval;
    rows.append(row);
    return w;
}

void Inspector::rebuild() {
    rows.clear();
    while (QLayoutItem* it = form->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    if (!clip) { form->addWidget(new QLabel("Выберите клип на таймлайне")); return; }
    form->addWidget(new QLabel(QString("<b>Transform / %1</b>").arg(clip->kind)));
    if (clip->kind == "text") addTextControls();
    if (clip->kind == "audio") {
        form->addWidget(makeRow("volume", -1, "volume", 0, 1, 1));
    } else {
        form->addWidget(makeRow("x", -1, "x", -2, 2, 0));
        form->addWidget(makeRow("y", -1, "y", -2, 2, 0));
        form->addWidget(makeRow("scale", -1, "scale", 0, 5, 1));
        form->addWidget(makeRow("rot", -1, "rot", -720, 720, 0));
        form->addWidget(makeRow("opacity", -1, "opacity", 0, 1, 1));
        addAnimationControls();
    }
    for (int i = 0; i < (int)clip->effects.size(); ++i) {
        const QList<ParamDef>* plist = effectParams(clip->effects[i].name);
        if (!plist) continue;
        QFrame* fr = new QFrame;
        QHBoxLayout* hl = new QHBoxLayout(fr);
        hl->setContentsMargins(0, 6, 0, 0);
        hl->addWidget(new QLabel("<b>fx: " + clip->effects[i].name + "</b>"), 1);
        QToolButton* rm = new QToolButton; rm->setText("✕");
        ClipPtr c = clip;
        connect(rm, &QToolButton::clicked, this, [this, c, i]() {
            emit aboutToChange(QString());
            if (i < (int)c->effects.size()) c->effects.erase(c->effects.begin() + i);
            graph->clearTarget(); rebuild(); emit changed();
        });
        hl->addWidget(rm);
        form->addWidget(fr);
        for (const ParamDef& pd : *plist)
            form->addWidget(makeRow(pd.name, i, pd.name, pd.lo, pd.hi, pd.def));
        if (!isAudioEffect(clip->effects[i].name))                       // поворот эффекта на любой угол 0..360°
            form->addWidget(makeRow("↻ поворот эффекта (°)", i, "_rot", 0, 360, 0));
    }
}

static double& animAngle(Clip& c, int s) { return s == 0 ? c.animInAngle : s == 1 ? c.animOutAngle : c.animLoopAngle; }
static double& animAmount(Clip& c, int s) { return s == 0 ? c.animInAmt : s == 1 ? c.animOutAmt : c.animLoopAmt; }
static QString& animName(Clip& c, int s) { return s == 0 ? c.animIn : s == 1 ? c.animOut : c.animLoop; }
static double* animCurvePtr(Clip& c, int s) { return s == 0 ? c.animInC : c.animOutC; }

void Inspector::addAnimationControls() {
    ClipPtr c = clip;
    if (c->animIn.isEmpty() && c->animOut.isEmpty() && c->animLoop.isEmpty()) return;
    form->addWidget(new QLabel("<b>Анимации</b> — угол крутится на 360°, у шариков на клипе — длительность"));
    static const char* titles[3] = {"▶ Вход", "◀ Выход", "∞ Постоянная"};
    for (int s = 0; s < 3; ++s) {
        if (animName(*c, s).isEmpty()) continue;
        QWidget* box = new QWidget;
        QGridLayout* g = new QGridLayout(box);
        g->setContentsMargins(0, 2, 0, 2);
        g->addWidget(new QLabel(QString("<b>%1: %2</b>").arg(titles[s], animName(*c, s))), 0, 0, 1, 3);
        QDial* dial = new QDial; dial->setRange(0, 359); dial->setWrapping(true); dial->setNotchesVisible(true); dial->setFixedSize(58, 58);
        dial->setValue((int)animAngle(*c, s) % 360);
        dial->setToolTip("Угол 0–360°: направление движения, а у спина — на сколько градусов крутить");
        QDoubleSpinBox* ang = new QDoubleSpinBox; ang->setRange(0, 360); ang->setDecimals(0); ang->setSuffix(" °"); ang->setValue(animAngle(*c, s));
        QDoubleSpinBox* amt = new QDoubleSpinBox; amt->setRange(0, 3); amt->setSingleStep(0.05); amt->setDecimals(2); amt->setValue(animAmount(*c, s));
        g->addWidget(dial, 1, 0, 3, 1);
        g->addWidget(new QLabel("Угол"), 1, 1); g->addWidget(ang, 1, 2);
        g->addWidget(new QLabel("Сила"), 2, 1); g->addWidget(amt, 2, 2);
        if (s < 2) {
            QDoubleSpinBox* dur = new QDoubleSpinBox; dur->setRange(0.05, qMax(c->dur, 0.1)); dur->setSingleStep(0.05); dur->setDecimals(2); dur->setSuffix(" с");
            dur->setValue(s == 0 ? c->animInDur : c->animOutDur);
            g->addWidget(new QLabel("Длительность"), 3, 1); g->addWidget(dur, 3, 2);
            connect(dur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, c, s](double v) {
                emit aboutToChange("anim");
                if (s == 0) c->animInDur = v; else c->animOutDur = v;
                emit changed();
            });
        }
        connect(ang, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, c, s, dial](double v) {
            emit aboutToChange("anim");
            animAngle(*c, s) = v;
            dial->blockSignals(true); dial->setValue((int)v % 360); dial->blockSignals(false);
            emit changed();
        });
        connect(dial, &QDial::valueChanged, this, [ang](int v) { ang->setValue(v); });
        connect(amt, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, c, s](double v) {
            emit aboutToChange("anim");
            animAmount(*c, s) = v;
            emit changed();
        });
        form->addWidget(box);
    }
    // кривая плавности входа / выхода
    if (!c->animIn.isEmpty() || !c->animOut.isEmpty()) {
        form->addWidget(new QLabel("<b>Кривая плавности анимации</b>"));
        QComboBox* which = new QComboBox;
        QList<int> ids;
        if (!c->animIn.isEmpty()) { which->addItem("Вход"); ids << 0; }
        if (!c->animOut.isEmpty()) { which->addItem("Выход"); ids << 1; }
        BezierEditor* ce = new BezierEditor;
        double* cv = animCurvePtr(*c, ids[0]);
        ce->setCurve(cv[0], cv[1], cv[2], cv[3]);
        connect(which, QOverload<int>::of(&QComboBox::activated), this, [c, ce, ids](int i) {
            double* p = animCurvePtr(*c, ids.value(i));
            ce->setCurve(p[0], p[1], p[2], p[3]);
        });
        connect(ce, &BezierEditor::aboutToChange, this, [this]() { emit aboutToChange("animcurve"); });
        connect(ce, &BezierEditor::curveChanged, this, [this, c, which, ids](double a, double b, double cc, double d) {
            double* p = animCurvePtr(*c, ids.value(which->currentIndex()));
            p[0] = a; p[1] = b; p[2] = cc; p[3] = d;
            emit changed();
        });
        form->addWidget(which); form->addWidget(ce);
    }
}

void Inspector::addTextControls() {
    ClipPtr tc = clip;
    QPlainTextEdit* le = new QPlainTextEdit(tc->text);
    le->setMaximumHeight(72);
    connect(le, &QPlainTextEdit::textChanged, this, [this, tc, le]() {
        emit aboutToChange("text");
        tc->text = le->toPlainText();
        emit changed();
    });
    form->addWidget(new QLabel("Текст (можно несколько строк):"));
    form->addWidget(le);
    form->addWidget(new QLabel("Шрифт:"));
    FontPicker* fp = new FontPicker;
    fp->setCurrent(tc->textFont);
    connect(fp, &FontPicker::fontChosen, this, [this, tc](QString f) { emit aboutToChange(QString()); tc->textFont = f; emit changed(); });
    form->addWidget(fp);
    QWidget* styleRow = new QWidget;
    QHBoxLayout* sr = new QHBoxLayout(styleRow);
    sr->setContentsMargins(0, 0, 0, 0);
    QCheckBox* bold = new QCheckBox("Жирный"); bold->setChecked(tc->textBold);
    QCheckBox* ital = new QCheckBox("Курсив"); ital->setChecked(tc->textItalic);
    QCheckBox* shad = new QCheckBox("Тень"); shad->setChecked(tc->textShadow);
    sr->addWidget(bold); sr->addWidget(ital); sr->addWidget(shad);
    connect(bold, &QCheckBox::toggled, this, [this, tc](bool v) { emit aboutToChange(QString()); tc->textBold = v; emit changed(); });
    connect(ital, &QCheckBox::toggled, this, [this, tc](bool v) { emit aboutToChange(QString()); tc->textItalic = v; emit changed(); });
    connect(shad, &QCheckBox::toggled, this, [this, tc](bool v) { emit aboutToChange(QString()); tc->textShadow = v; emit changed(); });
    form->addWidget(styleRow);
    QWidget* r2 = new QWidget;
    QHBoxLayout* h2 = new QHBoxLayout(r2);
    h2->setContentsMargins(0, 0, 0, 0);
    QPushButton* colorBtn = new QPushButton("Цвет");
    colorBtn->setStyleSheet("QPushButton{background:" + tc->textColor + ";color:#000;}");
    connect(colorBtn, &QPushButton::clicked, this, [this, tc, colorBtn]() {
        QColor c = QColorDialog::getColor(QColor(tc->textColor), this, "Цвет текста");
        if (!c.isValid()) return;
        emit aboutToChange(QString());
        tc->textColor = c.name();
        colorBtn->setStyleSheet("QPushButton{background:" + tc->textColor + ";color:#000;}");
        emit changed();
    });
    QPushButton* outBtn = new QPushButton("Контур");
    outBtn->setStyleSheet("QPushButton{background:" + tc->textOutlineColor + ";color:#fff;}");
    connect(outBtn, &QPushButton::clicked, this, [this, tc, outBtn]() {
        QColor c = QColorDialog::getColor(QColor(tc->textOutlineColor), this, "Цвет контура");
        if (!c.isValid()) return;
        emit aboutToChange(QString());
        tc->textOutlineColor = c.name();
        outBtn->setStyleSheet("QPushButton{background:" + tc->textOutlineColor + ";color:#fff;}");
        emit changed();
    });
    QDoubleSpinBox* outW = new QDoubleSpinBox; outW->setRange(0, 24); outW->setSuffix(" px"); outW->setValue(tc->textOutline);
    connect(outW, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, tc](double v) { emit aboutToChange("textout"); tc->textOutline = v; emit changed(); });
    h2->addWidget(colorBtn); h2->addWidget(outBtn); h2->addWidget(outW);
    form->addWidget(r2);
    QWidget* r3 = new QWidget;
    QHBoxLayout* h3 = new QHBoxLayout(r3);
    h3->setContentsMargins(0, 0, 0, 0);
    QDoubleSpinBox* szs = new QDoubleSpinBox;
    szs->setRange(0.2, 8.0); szs->setSingleStep(0.1); szs->setValue(tc->textSize);
    connect(szs, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, tc](double v) { emit aboutToChange("textsize"); tc->textSize = v; emit changed(); });
    QComboBox* al = new QComboBox; al->addItems({"Слева", "По центру", "Справа"}); al->setCurrentIndex(tc->textAlign);
    connect(al, QOverload<int>::of(&QComboBox::activated), this, [this, tc](int i) { emit aboutToChange(QString()); tc->textAlign = i; emit changed(); });
    h3->addWidget(new QLabel("Размер")); h3->addWidget(szs); h3->addWidget(al, 1);
    form->addWidget(r3);
}
