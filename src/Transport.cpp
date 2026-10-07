#include "Transport.h"
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QStyle>

void ClickSlider::mousePressEvent(QMouseEvent* e) {
    int v = QStyle::sliderValueFromPosition(minimum(), maximum(), (int)e->position().x(), width());
    setValue(v);
    emit sliderMoved(v);
    QSlider::mousePressEvent(e);
}

static QToolButton* mk(QWidget* parent, QStyle::StandardPixmap ic, const QString& tip) {
    QToolButton* b = new QToolButton(parent);
    b->setIcon(parent->style()->standardIcon(ic));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setIconSize(QSize(22, 22));
    return b;
}

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* v = new QVBoxLayout(this);
    v->setContentsMargins(6, 2, 6, 4);
    slider = new ClickSlider(Qt::Horizontal);
    slider->setRange(0, 10000);
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setStyleSheet("QSlider::groove:horizontal{height:6px;background:#3a3d42;border-radius:3px;}"
                          "QSlider::sub-page:horizontal{background:#e5484d;border-radius:3px;}"
                          "QSlider::handle:horizontal{background:#ff4d4f;width:14px;height:14px;margin:-5px 0;border-radius:7px;}");
    v->addWidget(slider);
    QHBoxLayout* h = new QHBoxLayout;
    h->setSpacing(2);
    QToolButton* toStart = mk(this, QStyle::SP_MediaSkipBackward, "Предыдущая склейка");
    QToolButton* back5 = mk(this, QStyle::SP_MediaSeekBackward, "Назад 5 с");
    QToolButton* back1 = mk(this, QStyle::SP_ArrowLeft, "Кадр назад (,)");
    play = mk(this, QStyle::SP_MediaPlay, "Играть / пауза (Space)");
    play->setAutoRaise(false);
    play->setFixedSize(44, 44);
    play->setIconSize(QSize(26, 26));
    play->setStyleSheet("QToolButton{border-radius:22px;background:#e5484d;}QToolButton:hover{background:#ff6369;}");
    QToolButton* fwd1 = mk(this, QStyle::SP_ArrowRight, "Кадр вперёд (.)");
    QToolButton* fwd5 = mk(this, QStyle::SP_MediaSeekForward, "Вперёд 5 с");
    QToolButton* toEnd = mk(this, QStyle::SP_MediaSkipForward, "Следующая склейка");
    for (QToolButton* b : {toStart, back5, back1}) h->addWidget(b);
    h->addWidget(play);
    for (QToolButton* b : {fwd1, fwd5, toEnd}) h->addWidget(b);
    time = new QLabel("00:00.00 / 00:00.00");
    time->setStyleSheet("color:#c8ccd0;font-family:monospace;");
    h->addSpacing(10); h->addWidget(time);
    h->addStretch(1);
    blade = new QToolButton; blade->setText("✂"); blade->setCheckable(true); blade->setFocusPolicy(Qt::NoFocus);
    blade->setToolTip("Ножницы: включи и кликай по таймлайну там, где резать (Esc — выключить)");
    blade->setStyleSheet("QToolButton{font-size:18px;padding:2px 8px;}QToolButton:checked{background:#3b6ea5;}");
    QToolButton* snap = new QToolButton; snap->setText("🧲"); snap->setCheckable(true); snap->setChecked(true);
    snap->setToolTip("Магнитная привязка"); snap->setFocusPolicy(Qt::NoFocus);
    QToolButton* rec = new QToolButton; rec->setText("●"); rec->setCheckable(true); rec->setFocusPolicy(Qt::NoFocus);
    rec->setToolTip("Запись движения: двигай объект мышью в плеере — создаются ключевые кадры");
    rec->setStyleSheet("QToolButton:checked{color:#ff4d4f;background:#4a2527;}");
    QToolButton* txt = new QToolButton; txt->setText("T"); txt->setFocusPolicy(Qt::NoFocus);
    txt->setToolTip("Добавить текст в позицию плейхеда (T)");
    txt->setStyleSheet("QToolButton{font-size:16px;font-weight:bold;padding:2px 10px;}");
    h->addWidget(txt); h->addWidget(blade); h->addWidget(snap); h->addWidget(rec);
    connect(txt, &QToolButton::clicked, this, &TransportBar::addTextRequested);
    v->addLayout(h);

    connect(play, &QToolButton::clicked, this, &TransportBar::playToggled);
    connect(toStart, &QToolButton::clicked, this, [this]() { emit jumpCut(-1); });
    connect(toEnd, &QToolButton::clicked, this, [this]() { emit jumpCut(1); });
    connect(back5, &QToolButton::clicked, this, [this]() { emit skipSeconds(-5); });
    connect(fwd5, &QToolButton::clicked, this, [this]() { emit skipSeconds(5); });
    connect(back1, &QToolButton::clicked, this, [this]() { emit stepFrames(-1); });
    connect(fwd1, &QToolButton::clicked, this, [this]() { emit stepFrames(1); });
    connect(blade, &QToolButton::toggled, this, &TransportBar::bladeToggled);
    connect(snap, &QToolButton::toggled, this, &TransportBar::snapToggled);
    connect(rec, &QToolButton::toggled, this, &TransportBar::recToggled);
    connect(slider, &QSlider::sliderMoved, this, [this](int val) {
        if (!updating) emit seekRequested(total * val / 10000.0);
    });
}

void TransportBar::setPlaying(bool on) {
    play->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

void TransportBar::setBlade(bool on) {
    blade->blockSignals(true); blade->setChecked(on); blade->blockSignals(false);
}

void TransportBar::setTime(double t, double tot) {
    total = tot;
    auto f = [](double x) { int m = (int)(x / 60); return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(x - m * 60, 5, 'f', 2, QChar('0')); };
    time->setText(f(t) + " / " + f(tot));
    updating = true;
    if (!slider->isSliderDown()) slider->setValue(tot > 0 ? (int)(10000.0 * qBound(0.0, t / tot, 1.0)) : 0);
    updating = false;
}
