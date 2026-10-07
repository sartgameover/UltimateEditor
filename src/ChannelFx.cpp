#include "ChannelFx.h"
#include "AudioFx.h"
#include <QPainter>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <cmath>
#include <algorithm>

void EqPlot::paintEvent(QPaintEvent*) {
    QPainter pr(this);
    pr.setRenderHint(QPainter::Antialiasing);
    pr.fillRect(rect(), QColor("#141518"));
    QRectF r = QRectF(rect()).adjusted(30, 8, -8, -20);
    pr.setPen(QColor("#2e3036"));
    for (int db = -18; db <= 18; db += 6) {
        double y = r.center().y() - db / 18.0 * r.height() / 2;
        pr.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        pr.setPen(QColor("#7d828a")); pr.drawText(QRectF(0, y - 7, 28, 14), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
        pr.setPen(QColor("#2e3036"));
    }
    static const double marks[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (double f : marks) {
        double x = r.left() + std::log10(f / 20.0) / 3.0 * r.width();
        pr.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        pr.setPen(QColor("#7d828a"));
        pr.drawText(QRectF(x - 20, r.bottom() + 2, 40, 14), Qt::AlignCenter, f >= 1000 ? QString("%1k").arg(f / 1000) : QString::number(f));
        pr.setPen(QColor("#2e3036"));
    }
    QPolygonF poly;
    poly << QPointF(r.left(), r.center().y());
    for (int i = 0; i <= 240; ++i) {
        double f = 20.0 * std::pow(1000.0, i / 240.0);
        double db = std::max(-18.0, std::min(18.0, eqResponseDb(p, f)));
        poly << QPointF(r.left() + i / 240.0 * r.width(), r.center().y() - db / 18.0 * r.height() / 2);
    }
    poly << QPointF(r.right(), r.center().y());
    pr.setPen(Qt::NoPen);
    pr.setBrush(QColor(77, 171, 247, 60));
    pr.drawPolygon(poly);
    pr.setPen(QPen(QColor("#4dabf7"), 2));
    pr.setBrush(Qt::NoBrush);
    pr.drawPolyline(poly);
}

ChannelFxDialog::ChannelFxDialog(Project* p, const QString& ch, QWidget* parent) : QDialog(parent), project(p), channel(ch) {
    setWindowTitle("Звуковая студия — канал " + ch);
    resize(760, 560);
    QHBoxLayout* root = new QHBoxLayout(this);
    // левая колонка — цепочка вставок
    QVBoxLayout* left = new QVBoxLayout;
    left->addWidget(new QLabel("<b>Вставки канала " + ch + "</b> (сверху вниз)"));
    list = new QListWidget;
    left->addWidget(list, 1);
    addBox = new QComboBox;
    for (const AudioFxDef& d : audioFxDefs()) addBox->addItem(d.name);
    QPushButton* addBtn = new QPushButton("＋ Добавить эффект");
    QHBoxLayout* mv = new QHBoxLayout;
    QPushButton* up = new QPushButton("▲"); QPushButton* down = new QPushButton("▼"); QPushButton* del = new QPushButton("✕ Убрать");
    bypass = new QPushButton("Выкл/вкл"); bypass->setCheckable(true);
    mv->addWidget(up); mv->addWidget(down); mv->addWidget(del);
    left->addWidget(addBox); left->addWidget(addBtn); left->addLayout(mv); left->addWidget(bypass);
    root->addLayout(left, 1);
    // правая — параметры и график
    QVBoxLayout* right = new QVBoxLayout;
    right->addWidget(new QLabel("<b>Параметры</b>"));
    paramsBox = new QWidget;
    paramsLay = new QVBoxLayout(paramsBox);
    QScrollArea* sc = new QScrollArea; sc->setWidgetResizable(true); sc->setWidget(paramsBox);
    right->addWidget(sc, 1);
    eq = new EqPlot;
    right->addWidget(eq);
    root->addLayout(right, 2);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const AudioFxDef* d = findAudioFx(addBox->currentText());
        if (!d) return;
        EffectInst e; e.name = d->name;
        for (const ParamDef& pd : d->params) e.params[pd.name] = pd.def;
        chain().push_back(e);
        rebuildList((int)chain().size() - 1);
    });
    connect(del, &QPushButton::clicked, this, [this]() {
        int r = list->currentRow();
        if (r < 0 || r >= (int)chain().size()) return;
        chain().erase(chain().begin() + r);
        rebuildList(std::min(r, (int)chain().size() - 1));
    });
    connect(up, &QPushButton::clicked, this, [this]() {
        int r = list->currentRow();
        if (r <= 0 || r >= (int)chain().size()) return;
        std::swap(chain()[r], chain()[r - 1]);
        rebuildList(r - 1);
    });
    connect(down, &QPushButton::clicked, this, [this]() {
        int r = list->currentRow();
        if (r < 0 || r + 1 >= (int)chain().size()) return;
        std::swap(chain()[r], chain()[r + 1]);
        rebuildList(r + 1);
    });
    connect(bypass, &QPushButton::toggled, this, [this](bool off) {
        int r = list->currentRow();
        if (r < 0 || r >= (int)chain().size()) return;
        chain()[r].params["_bypass"] = off ? 1.0 : 0.0;
        rebuildList(r);
    });
    connect(list, &QListWidget::currentRowChanged, this, [this](int) { rebuildParams(); });
    rebuildList(chain().empty() ? -1 : 0);
}

std::vector<EffectInst>& ChannelFxDialog::chain() {
    return channel == "Master" ? project->masterFx : project->trackFx[channel];
}

void ChannelFxDialog::rebuildList(int select) {
    list->blockSignals(true);
    list->clear();
    for (const EffectInst& e : chain())
        list->addItem(e.name + (e.params.value("_bypass", 0.0) > 0.5 ? "  (выкл)" : ""));
    if (select >= 0 && select < list->count()) list->setCurrentRow(select);
    list->blockSignals(false);
    rebuildParams();
}

void ChannelFxDialog::rebuildParams() {
    while (QLayoutItem* it = paramsLay->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    int r = list->currentRow();
    eq->setVisible(false);
    bypass->blockSignals(true); bypass->setChecked(false); bypass->blockSignals(false);
    if (r < 0 || r >= (int)chain().size()) { paramsLay->addWidget(new QLabel("Добавь эффект слева: EQ, компрессор, ревербератор…")); paramsLay->addStretch(1); return; }
    const EffectInst& cur = chain()[r];
    const AudioFxDef* d = findAudioFx(cur.name);
    if (!d) return;
    bypass->blockSignals(true); bypass->setChecked(cur.params.value("_bypass", 0.0) > 0.5); bypass->blockSignals(false);
    QGridLayout* g = new QGridLayout;
    QWidget* holder = new QWidget; holder->setLayout(g);
    int row = 0;
    bool isEq = (d->name == "EQ 4-band");
    for (const ParamDef& pd : d->params) {
        QLabel* lab = new QLabel(pd.name);
        QSlider* s = new QSlider(Qt::Horizontal);
        s->setRange(0, 1000);
        double v = cur.params.value(pd.name, pd.def);
        s->setValue((int)std::lround((v - pd.lo) / (pd.hi - pd.lo) * 1000.0));
        QLabel* val = new QLabel(QString::number(v, 'f', 2));
        val->setMinimumWidth(56);
        g->addWidget(lab, row, 0); g->addWidget(s, row, 1); g->addWidget(val, row, 2);
        QString pname = pd.name; double lo = pd.lo, hi = pd.hi;
        connect(s, &QSlider::valueChanged, this, [this, r, pname, lo, hi, val, isEq](int sv) {
            if (r >= (int)chain().size()) return;
            double nv = lo + (hi - lo) * sv / 1000.0;
            chain()[r].params[pname] = nv;
            val->setText(QString::number(nv, 'f', 2));
            if (isEq) {
                const AudioFxDef* dd = findAudioFx("EQ 4-band");
                double arr[10];
                for (int i = 0; i < 10; ++i) arr[i] = chain()[r].params.value(dd->params[i].name, dd->params[i].def);
                eq->setParams(arr);
            }
        });
        ++row;
    }
    paramsLay->addWidget(holder);
    paramsLay->addStretch(1);
    if (isEq) {
        double arr[10];
        for (int i = 0; i < 10; ++i) arr[i] = cur.params.value(d->params[i].name, d->params[i].def);
        eq->setParams(arr);
        eq->setVisible(true);
    }
}
