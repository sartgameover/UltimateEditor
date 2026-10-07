#include "Mixer.h"
#include <QPainter>
#include <QSlider>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <cmath>

void LevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#101113"));
    int h = (int)(height() * qMin(level, 1.0f));
    QLinearGradient g(0, height(), 0, 0);
    g.setColorAt(0.0, QColor("#46a758")); g.setColorAt(0.7, QColor("#f5a524")); g.setColorAt(1.0, QColor("#e5484d"));
    p.fillRect(0, height() - h, width(), h, g);
}

MixerPanel::MixerPanel(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    row = new QHBoxLayout(this);
    row->setContentsMargins(8, 8, 8, 8);
    rebuild();
}

static QString dbText(double g) { return g <= 0.0001 ? QString("-∞ dB") : QString("%1 dB").arg(20.0 * std::log10(g), 0, 'f', 1); }

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
        QLabel* title = new QLabel("<b>" + n + "</b>"); title->setAlignment(Qt::AlignCenter);
        col->addWidget(title);
        QToolButton* fxb = new QToolButton; fxb->setText("FX ▸"); fxb->setToolTip("Звуковая студия канала: EQ, компрессор, реверб…");
        col->addWidget(fxb);
        if (!isMaster) {
            QSlider* pan = new QSlider(Qt::Horizontal);
            pan->setRange(-100, 100); pan->setValue((int)std::lround(project->trackPan.value(n, 0.0) * 100));
            pan->setToolTip("Панорама (двойной клик — в центр)");
            col->addWidget(pan);
            QString name = n;
            connect(pan, &QSlider::valueChanged, this, [this, name](int v) { project->trackPan[name] = v / 100.0; });
        }
        Strip s; s.name = n;
        s.fader = new QSlider(Qt::Vertical);
        s.fader->setRange(0, 100); s.fader->setMinimumHeight(100);
        s.fader->setTickPosition(QSlider::TicksRight); s.fader->setTickInterval(25);
        double g = isMaster ? project->master : project->trackGain.value(n, 1.0);
        s.fader->setValue((int)std::lround(g * 100));
        s.meter = new LevelMeter;
        s.db = new QLabel(dbText(g)); s.db->setAlignment(Qt::AlignCenter);
        QHBoxLayout* fm = new QHBoxLayout;
        fm->addWidget(s.fader); fm->addWidget(s.meter);
        col->addLayout(fm, 1);
        col->addWidget(s.db);
        if (!isMaster) {
            QToolButton* m = new QToolButton; m->setText("M"); m->setCheckable(true); m->setToolTip("Mute");
            QToolButton* so = new QToolButton; so->setText("S"); so->setCheckable(true); so->setToolTip("Solo");
            m->setChecked(project->mute.value(n, false)); so->setChecked(project->solo.value(n, false));
            QHBoxLayout* ms = new QHBoxLayout; ms->addWidget(m); ms->addWidget(so);
            col->addLayout(ms);
            QString name = n;
            connect(m, &QToolButton::toggled, this, [this, name](bool on) { project->mute[name] = on; });
            connect(so, &QToolButton::toggled, this, [this, name](bool on) { project->solo[name] = on; });
        }
        row->addWidget(colw);
        cols.append(colw);
        QString name = n;
        connect(s.fader, &QSlider::valueChanged, this, [this, name](int v) { setGain(name, v); });
        connect(fxb, &QToolButton::clicked, this, [this, name]() { emit openFx(name); });
        strips.insert(n, s);
    }
}

void MixerPanel::setGain(const QString& name, int percent) {
    double g = percent / 100.0;
    if (name == "Master") project->master = g; else project->trackGain[name] = g;
    if (strips.contains(name)) strips[name].db->setText(dbText(g));
}

void MixerPanel::updateMeters(const QMap<QString, float>& levels, bool playing) {
    for (auto it = strips.begin(); it != strips.end(); ++it)
        it.value().meter->setLevel(playing ? levels.value(it.key(), 0.f) : 0.f);
}
