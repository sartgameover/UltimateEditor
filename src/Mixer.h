#pragma once
#include "Project.h"
#include <QWidget>
#include <QMap>

class QSlider; class QLabel; class QToolButton; class QHBoxLayout;

class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr) : QWidget(parent) { setFixedWidth(12); setMinimumHeight(90); }
    void setLevel(float l) { level = qMax(l, level * 0.88f); update(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float level = 0;
};

// Звуковой микшер: фейдер, панорама, Mute/Solo, кнопка FX («звуковая студия» канала), реальные пик-метры из аудио-движка.
class MixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit MixerPanel(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p) { project = p; rebuild(); }
    void rebuild();
    void updateMeters(const QMap<QString, float>& levels, bool playing);
signals:
    void openFx(QString channel);
private:
    struct Strip { QString name; QSlider* fader; QLabel* db; LevelMeter* meter; };
    void setGain(const QString& name, int percent);
    Project* project;
    QHBoxLayout* row;
    QList<QWidget*> cols;
    QMap<QString, Strip> strips;
};
