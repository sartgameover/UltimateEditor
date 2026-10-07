#pragma once
#include "Project.h"
#include <QDialog>
#include <QWidget>

class QListWidget; class QComboBox; class QVBoxLayout; class QPushButton;

// График АЧХ эквалайзера
class EqPlot : public QWidget {
public:
    explicit EqPlot(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(150); for (int i = 0; i < 10; ++i) p[i] = 0; }
    void setParams(const double* v) { for (int i = 0; i < 10; ++i) p[i] = v[i]; update(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    double p[10];
};

// «Звуковая студия» канала (как в Fairlight / FL Studio): цепочка вставок, параметры, график EQ.
class ChannelFxDialog : public QDialog {
    Q_OBJECT
public:
    ChannelFxDialog(Project* p, const QString& channel, QWidget* parent = nullptr);
private:
    std::vector<EffectInst>& chain();
    void rebuildList(int select);
    void rebuildParams();
    Project* project;
    QString channel;
    QListWidget* list;
    QComboBox* addBox;
    QWidget* paramsBox;
    QVBoxLayout* paramsLay;
    EqPlot* eq;
    QPushButton* bypass;
};
