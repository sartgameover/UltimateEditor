#include "Water.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <cmath>

WaterProgress::WaterProgress(QWidget* parent) : QWidget(parent) {
    setMinimumSize(170, 170);
    timer.setInterval(16);
    connect(&timer, &QTimer::timeout, this, [this]() {
        phase += 0.12;
        shown += (target - shown) * 0.12;                 // уровень воды плавно догоняет значение
        update();
    });
    timer.start();
}

void WaterProgress::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double d = qMin(width(), height()) - 16;
    QRectF circle((width() - d) / 2, (height() - d) / 2, d, d);
    QPainterPath clip; clip.addEllipse(circle);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#15171a"));
    p.drawEllipse(circle);
    p.save();
    p.setClipPath(clip);
    double level = circle.bottom() - d * (shown / 100.0);
    for (int layer = 0; layer < 2; ++layer) {                // две волны с разной фазой
        QPainterPath w;
        w.moveTo(circle.left(), circle.bottom() + 2);
        double amp = (layer == 0 ? 6.0 : 4.0) * (shown > 1 && shown < 99 ? 1.0 : 0.3);
        for (double x = circle.left(); x <= circle.right() + 2; x += 3)
            w.lineTo(x, level + std::sin(x * 0.045 + phase * (layer == 0 ? 1.0 : -1.3) + layer) * amp);
        w.lineTo(circle.right() + 2, circle.bottom() + 2);
        w.closeSubpath();
        QLinearGradient g(0, level, 0, circle.bottom());
        g.setColorAt(0, layer == 0 ? QColor(77, 171, 247, 230) : QColor(40, 120, 220, 150));
        g.setColorAt(1, layer == 0 ? QColor(20, 70, 160, 240) : QColor(15, 50, 130, 170));
        p.setBrush(g);
        p.drawPath(w);
    }
    p.restore();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#4dabf7"), 3));
    p.drawEllipse(circle);
    QFont f = font(); f.setPixelSize((int)(d * 0.24)); f.setBold(true); p.setFont(f);
    QString txt = QString::number((int)std::lround(shown)) + "%";
    p.setPen(QColor(0, 0, 0, 140));
    p.drawText(circle.translated(1.5, 1.5), Qt::AlignCenter, txt);
    p.setPen(Qt::white);
    p.drawText(circle, Qt::AlignCenter, txt);
}
