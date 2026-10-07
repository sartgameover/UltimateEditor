#include "Panels.h"
#include "Effects.h"
#include "AudioFx.h"
#include <QColorDialog>
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
    for (const EffectDef& d : effectDefs()) {
        QTreeWidgetItem* it = new QTreeWidgetItem(QStringList(d.name));
        it->setData(0, Qt::UserRole, "fx");
        category(d.category)->addChild(it);
    }
    for (const AudioFxDef& d : audioFxDefs()) {
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

// ================= KeyframeGraph =================
KeyframeGraph::KeyframeGraph(QWidget* parent) : QWidget(parent) { setMinimumHeight(140); }

void KeyframeGraph::setTarget(const ClipPtr& c, int idx, const QString& k, const QString& l, double a, double b) {
    clip = c; fxIndex = idx; key = k; label = l; lo = a; hi = b; dragIdx = -1; update();
}
void KeyframeGraph::clearTarget() { clip.reset(); key.clear(); update(); }

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
    p.drawText(10, 16, "График: " + (label.isEmpty() ? QString("выберите параметр (кнопка 📈)") : label) + "   [2×клик — ключ, ПКМ — режим]");
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
    for (const Keyframe& k : *kl) {
        p.setBrush(k.mode == 2 ? QColor("#e5484d") : k.mode == 1 ? QColor("#46a758") : QColor("#ffd54a"));
        p.setPen(Qt::black);
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
            if (e->button() == Qt::RightButton) { (*kl)[i].mode = ((*kl)[i].mode + 1) % 3; update(); emit edited(); }
            else dragIdx = i;
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
    if (kl) std::sort(kl->begin(), kl->end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
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
    lay->addWidget(sc, 1);
    lay->addWidget(graph);
    lay->addWidget(pb);
    rebuild();
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
    if (clip->kind == "text") {
        QLineEdit* le = new QLineEdit(clip->text);
        ClipPtr tc = clip;
        connect(le, &QLineEdit::textEdited, this, [this, tc](const QString& s) {
            emit aboutToChange("text");
            tc->text = s;
            emit changed();
        });
        form->addWidget(new QLabel("Текст:"));
        form->addWidget(le);
        QPushButton* colorBtn = new QPushButton("Цвет текста");
        colorBtn->setStyleSheet("QPushButton{background:" + clip->textColor + ";color:#000;}");
        connect(colorBtn, &QPushButton::clicked, this, [this, tc, colorBtn]() {
            QColor c = QColorDialog::getColor(QColor(tc->textColor), this, "Цвет текста");
            if (!c.isValid()) return;
            emit aboutToChange(QString());
            tc->textColor = c.name();
            colorBtn->setStyleSheet("QPushButton{background:" + tc->textColor + ";color:#000;}");
            emit changed();
        });
        form->addWidget(colorBtn);
        QDoubleSpinBox* szs = new QDoubleSpinBox;
        szs->setRange(0.3, 5.0); szs->setSingleStep(0.1); szs->setValue(clip->textSize);
        connect(szs, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, tc](double v) {
            emit aboutToChange("textsize");
            tc->textSize = v;
            emit changed();
        });
        QHBoxLayout* sl = new QHBoxLayout;
        QWidget* sw = new QWidget; sw->setLayout(sl);
        sl->setContentsMargins(0, 0, 0, 0);
        sl->addWidget(new QLabel("Размер шрифта"), 1); sl->addWidget(szs);
        form->addWidget(sw);
    }
    if (clip->kind == "audio") {
        form->addWidget(makeRow("volume", -1, "volume", 0, 1, 1));
    } else {
        form->addWidget(makeRow("x", -1, "x", -2, 2, 0));
        form->addWidget(makeRow("y", -1, "y", -2, 2, 0));
        form->addWidget(makeRow("scale", -1, "scale", 0, 5, 1));
        form->addWidget(makeRow("rot", -1, "rot", -720, 720, 0));
        form->addWidget(makeRow("opacity", -1, "opacity", 0, 1, 1));
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
    }
}
