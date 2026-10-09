#pragma once
#include "Project.h"
#include <QWidget>
#include <QMap>
#include <QElapsedTimer>

class QSlider; class QLabel; class QToolButton; class QHBoxLayout; class QDoubleSpinBox;

// Фейдер в дБ: слева шкала (+6 … -60 дБ, как линейка), два пик-метра (L/R) с удержанием пика, ползунок до +6 дБ (200%).
class DbFader : public QWidget {
    Q_OBJECT
public:
    explicit DbFader(QWidget* parent = nullptr);
    void setGain(double g);                         // линейное значение (1.0 = 0 дБ, максимум ≈ 2.0 = +6 дБ)
    double gain() const;
    double dB() const { return db; }
    void setLevels(float l, float r);
signals:
    void gainChanged(double g);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    double yToDb(double y) const;
    double dbToY(double d) const;
    void setDbInternal(double d);
    static constexpr double DB_MIN = -60.0, DB_MAX = 6.0;
    double db = 0.0;
    float lvl[2] = {-100, -100}, peak[2] = {-100, -100};
    int hold[2] = {0, 0};
};

// Звуковой микшер: иконки, панорама (из какой колонки играет), фейдер в дБ с цифрами, Mute/Solo, FX.
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
    struct Strip { QString name; DbFader* fader; QDoubleSpinBox* spin; QLabel* percent; };
    void setGain(const QString& name, double g);
    Project* project;
    QHBoxLayout* row;
    QList<QWidget*> cols;
    QMap<QString, Strip> strips;
};
