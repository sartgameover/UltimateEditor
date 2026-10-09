#include "Mixer.h"
#include <QPainter>
#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <cmath>

static double lin2db(double g) { return g <= 1e-6 ? -200.0 : 20.0 * std::log10(g); }
static double db2lin(double d) { return d <= -59.5 ? 0.0 : std::pow(10.0, d / 20.0); }

// ================= DbFader =================
DbFader::DbFader(QWidget* parent) : QWidget(parent) { setMinimumSize(96, 190); setMouseTracking(false); }

double DbFader::gain() const { return db2lin(db); }
void DbFader::setGain(double g) { db = g <= 1e-6 ? DB_MIN : qBound(DB_MIN, lin2db(g), DB_MAX); update(); }

void DbFader::setLevels(float l, float r) {
    float in[2] = {l, r};
    for (int i = 0; i < 2; ++i) {
        float d = (float)lin2db(in[i]);
        lvl[i] = qMax(d, lvl[i] - 2.5f);                     // плавный спад
        if (d >= peak[i]) { peak[i] = d; hold[i] = 40; }      // удержание пика ~1.3 с
        else if (hold[i] > 0) --hold[i];
        else peak[i] = qMax(peak[i] - 1.0f, -100.f);
    }
    update();
}

double DbFader::dbToY(double d) const { const double top = 8, h = height() - 16; return top + (DB_MAX - d) / (DB_MAX - DB_MIN) * h; }
double DbFader::yToDb(double y) const { const double top = 8, h = height() - 16; return DB_MAX - (y - top) / h * (DB_MAX - DB_MIN); }

void DbFader::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#17181b"));
    static const int marks[] = {6, 0, -6, -12, -18, -24, -36, -48, -60};
    QFont f = font(); f.setPointSize(7); p.setFont(f);
    for (int m : marks) {                                      // шкала-линейка слева
        double y = dbToY(m);
        p.setPen(m == 0 ? QColor("#f5a524") : QColor("#5b616b"));
        p.drawLine(QPointF(26, y), QPointF(m % 12 == 0 || m == 6 ? 34 : 31, y));
        p.drawText(QRectF(0, y - 7, 24, 14), Qt::AlignRight | Qt::AlignVCenter, m > 0 ? QString("+%1").arg(m) : QString::number(m));
    }
    // два метра L / R
    const double mx[2] = {40, 54};
    for (int i = 0; i < 2; ++i) {
        QRectF bar(mx[i], dbToY(DB_MAX), 10, dbToY(DB_MIN) - dbToY(DB_MAX));
        p.fillRect(bar, QColor("#0d0e10"));
        double top = dbToY(qBound((double)DB_MIN, (double)lvl[i], DB_MAX));
        QLinearGradient g(0, bar.bottom(), 0, bar.top());
        g.setColorAt(0.0, QColor("#2ea043")); g.setColorAt(0.70, QColor("#d9b52b")); g.setColorAt(0.92, QColor("#e5484d"));
        p.fillRect(QRectF(bar.left(), top, bar.width(), bar.bottom() - top), g);
        if (peak[i] > DB_MIN) { double py = dbToY(qMin((double)peak[i], DB_MAX)); p.fillRect(QRectF(bar.left(), py, bar.width(), 2), peak[i] > 0 ? QColor("#ff6b6b") : QColor("#e6e6e6")); }
        p.setPen(QColor("#6b7280")); p.drawText(QRectF(bar.left(), height() - 12, 10, 12), Qt::AlignCenter, i == 0 ? "L" : "R");
    }
    // дорожка и ползунок
    double tx = 80;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#0d0e10"));
    p.drawRoundedRect(QRectF(tx - 3, dbToY(DB_MAX), 6, dbToY(DB_MIN) - dbToY(DB_MAX)), 3, 3);
    p.setPen(QColor("#f5a524"));
    p.drawLine(QPointF(tx - 9, dbToY(0)), QPointF(tx + 9, dbToY(0)));            // отметка 0 дБ
    double ty = dbToY(db);
    QLinearGradient tg(0, ty - 8, 0, ty + 8);
    tg.setColorAt(0, QColor("#d7dbe2")); tg.setColorAt(1, QColor("#8b919b"));
    p.setBrush(tg); p.setPen(QPen(QColor("#202124"), 1));
    p.drawRoundedRect(QRectF(tx - 12, ty - 8, 24, 16), 3, 3);
    p.setPen(QColor("#202124"));
    p.drawLine(QPointF(tx - 9, ty), QPointF(tx + 9, ty));
}

void DbFader::setDbInternal(double d) {
    d = qBound(DB_MIN, d, DB_MAX);
    if (std::fabs(d) < 0.7) d = 0.0;                          // «залипание» на 0 дБ
    db = d;
    update();
    emit gainChanged(gain());
}
void DbFader::mousePressEvent(QMouseEvent* e) { setDbInternal(yToDb(e->position().y())); }
void DbFader::mouseMoveEvent(QMouseEvent* e) { if (e->buttons() & Qt::LeftButton) setDbInternal(yToDb(e->position().y())); }
void DbFader::wheelEvent(QWheelEvent* e) { setDbInternal(db + (e->angleDelta().y() > 0 ? 0.5 : -0.5)); }
void DbFader::mouseDoubleClickEvent(QMouseEvent*) { setDbInternal(0.0); }

// ================= MixerPanel =================
MixerPanel::MixerPanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    row = new QHBoxLayout(this);
    row->setContentsMargins(8, 8, 8, 8);
    rebuild();
}

static QString panText(int v) { return v == 0 ? "Центр" : v < 0 ? QString("Лев %1%").arg(-v) : QString("Прав %1%").arg(v); }

void MixerPanel::rebuild() {
    for (QWidget* w : cols) { row->removeWidget(w); w->deleteLater(); }
    cols.clear(); strips.clear();
    QStringList names = project->audioTracks();
    names << "Master";
    for (const QString& n : names) {
        const bool isMaster = (n == "Master");
        QWidget* colw = new QWidget;
        QVBoxLayout* col = new QVBoxLayout(colw);
        col->setContentsMargins(2, 0, 2, 0);
        QLabel* title = new QLabel((isMaster ? "🔊 " : "🎵 ") + QString("<b>%1</b>").arg(n));
        title->setAlignment(Qt::AlignCenter);
        title->setToolTip(isMaster ? "Общая громкость всей программы" : "Звуковая дорожка " + n);
        col->addWidget(title);
        QToolButton* fxb = new QToolButton; fxb->setText("🎛 FX"); fxb->setToolTip("Звуковая студия канала: EQ, компрессор, реверб…");
        col->addWidget(fxb);
        QString name = n;
        if (!isMaster) {
            QHBoxLayout* pr = new QHBoxLayout;
            QLabel* l = new QLabel("L"); l->setToolTip("Левая колонка");
            QLabel* r = new QLabel("R"); r->setToolTip("Правая колонка");
            QSlider* pan = new QSlider(Qt::Horizontal);
            pan->setRange(-100, 100); pan->setValue((int)std::lround(project->trackPan.value(n, 0.0) * 100));
            pan->setToolTip("Панорама: из какой колонки играет звук — левой (L), правой (R) или обеих (центр). Двойной клик по надписи — в центр.");
            QLabel* pv = new QLabel(panText(pan->value())); pv->setAlignment(Qt::AlignCenter);
            pr->addWidget(l); pr->addWidget(pan, 1); pr->addWidget(r);
            col->addLayout(pr);
            col->addWidget(pv);
            connect(pan, &QSlider::valueChanged, this, [this, name, pv](int v) { project->trackPan[name] = v / 100.0; pv->setText(panText(v)); });
        }
        Strip s; s.name = n;
        s.fader = new DbFader;
        double g = isMaster ? project->master : project->trackGain.value(n, 1.0);
        s.fader->setGain(g);
        col->addWidget(s.fader, 1);
        s.spin = new QDoubleSpinBox;
        s.spin->setRange(-60.0, 6.0); s.spin->setDecimals(1); s.spin->setSingleStep(0.5); s.spin->setSuffix(" дБ");
        s.spin->setValue(g <= 1e-6 ? -60.0 : lin2db(g));
        s.spin->setToolTip("Громкость в децибелах. 0 дБ — как в исходнике, максимум +6 дБ (200%).");
        s.percent = new QLabel(QString("%1%").arg((int)std::lround(g * 100))); s.percent->setAlignment(Qt::AlignCenter);
        col->addWidget(s.spin); col->addWidget(s.percent);
        if (!isMaster) {
            QToolButton* m = new QToolButton; m->setText("🔇"); m->setCheckable(true); m->setToolTip("Mute — выключить звук дорожки");
            QToolButton* so = new QToolButton; so->setText("🎧"); so->setCheckable(true); so->setToolTip("Solo — слушать только эту дорожку");
            m->setChecked(project->mute.value(n, false)); so->setChecked(project->solo.value(n, false));
            m->setStyleSheet("QToolButton:checked{background:#a33;}"); so->setStyleSheet("QToolButton:checked{background:#2a7;}");
            QHBoxLayout* ms = new QHBoxLayout; ms->addWidget(m); ms->addWidget(so);
            col->addLayout(ms);
            connect(m, &QToolButton::toggled, this, [this, name](bool on) { project->mute[name] = on; });
            connect(so, &QToolButton::toggled, this, [this, name](bool on) { project->solo[name] = on; });
        }
        row->addWidget(colw);
        cols.append(colw);
        strips.insert(n, s);
        DbFader* fd = s.fader; QDoubleSpinBox* sp = s.spin; QLabel* pc = s.percent;
        connect(fd, &DbFader::gainChanged, this, [this, name, sp, pc](double gg) {
            setGain(name, gg);
            sp->blockSignals(true); sp->setValue(gg <= 1e-6 ? -60.0 : lin2db(gg)); sp->blockSignals(false);
            pc->setText(QString("%1%").arg((int)std::lround(gg * 100)));
        });
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, name, fd, pc](double d) {
            double gg = db2lin(d);
            fd->setGain(gg); setGain(name, gg);
            pc->setText(QString("%1%").arg((int)std::lround(gg * 100)));
        });
        connect(fxb, &QToolButton::clicked, this, [this, name]() { emit openFx(name); });
    }
}

void MixerPanel::setGain(const QString& name, double g) {
    if (name == "Master") project->master = g; else project->trackGain[name] = g;
}

void MixerPanel::updateMeters(const QMap<QString, float>& levels, bool playing) {
    for (auto it = strips.begin(); it != strips.end(); ++it) {
        const QString& n = it.key();
        it.value().fader->setLevels(playing ? levels.value(n + ":L", 0.f) : 0.f, playing ? levels.value(n + ":R", 0.f) : 0.f);
    }
}
