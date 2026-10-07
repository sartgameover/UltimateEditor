#pragma once
#include <QWidget>
#include <QSlider>

class QToolButton; class QLabel;

class ClickSlider : public QSlider {
    Q_OBJECT
public:
    explicit ClickSlider(Qt::Orientation o, QWidget* parent = nullptr) : QSlider(o, parent) {}
protected:
    void mousePressEvent(QMouseEvent* e) override;
};

// Панель под видео: кнопки как на YouTube (▶/⏸, назад/вперёд), перемотка, ножницы, ползунок-«красная штучка».
class TransportBar : public QWidget {
    Q_OBJECT
public:
    explicit TransportBar(QWidget* parent = nullptr);
    void setPlaying(bool on);
    void setTime(double t, double total);
    void setBlade(bool on);
signals:
    void playToggled();
    void seekRequested(double sec);
    void stepFrames(int n);
    void skipSeconds(double s);
    void jumpCut(int dir);
    void bladeToggled(bool on);
    void recToggled(bool on);
    void snapToggled(bool on);
    void addTextRequested();
private:
    QToolButton* play; QToolButton* blade; ClickSlider* slider; QLabel* time;
    double total = 0; bool updating = false;
};
