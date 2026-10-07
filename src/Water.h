#pragma once
#include <QWidget>
#include <QTimer>

// Круг, который заполняется «водой» с волнами; по центру — проценты.
class WaterProgress : public QWidget {
public:
    explicit WaterProgress(QWidget* parent = nullptr);
    void setValue(int v) { target = qBound(0, v, 100); }
    int value() const { return (int)shown; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    double shown = 0, target = 0, phase = 0;
    QTimer timer;
};
