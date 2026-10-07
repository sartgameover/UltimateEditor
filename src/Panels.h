#pragma once
#include "Project.h"
#include <QListWidget>
#include <QTreeWidget>
#include <QWidget>
#include <functional>

class QVBoxLayout;
class QDoubleSpinBox;

// ---- Project Assets (вкладка «Медиа») ----
class AssetsPanel : public QListWidget {
    Q_OBJECT
public:
    explicit AssetsPanel(QWidget* parent = nullptr);
    void addAsset(const Asset& a);
    void clearAll() { clear(); }
signals:
    void filesAdded(QStringList paths);
    void assetActivated(int assetId);
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};

// ---- вкладка «Шейдеры/Эффекты»: hover = мгновенный предпросмотр в плеере ----
class EffectsTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit EffectsTree(QWidget* parent = nullptr);
    void rebuild();
signals:
    void hovered(QString effectName);          // "" — курсор ушёл
    void applyEffect(QString name);
    void applyPreset(QString name);
protected:
    void leaveEvent(QEvent* e) override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;      // перетаскивание эффекта на клип
};

// ---- вкладка «Анимации» ----
class AnimationsList : public QListWidget {
    Q_OBJECT
public:
    explicit AnimationsList(QWidget* parent = nullptr);
signals:
    void applyAnimation(QString name);
protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;       // перетаскивание анимации на клип
};

// ---- график ключевых кадров ----
class KeyframeGraph : public QWidget {
    Q_OBJECT
public:
    explicit KeyframeGraph(QWidget* parent = nullptr);
    void setTarget(const ClipPtr& c, int effectIndex, const QString& key, const QString& label, double lo, double hi);
    void clearTarget();
signals:
    void edited();
    void aboutToEdit();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    KeyList* list() const;
    QPointF toPt(const Keyframe& k) const;
    void fromPt(const QPointF& p, double& t, double& v) const;
    ClipPtr clip; int fxIndex = -1; QString key, label;
    double lo = 0, hi = 1; int dragIdx = -1;
};

// ---- инспектор клипа: transform, эффекты, ключевые кадры ----
class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(QWidget* parent = nullptr);
    void setClip(const ClipPtr& c);
    void setNow(double t);                     // обновляет значения параметров под текущее время
    void rebuild();
    void refreshValues();
signals:
    void changed();
    void presetSaved();
    void aboutToChange(QString key);          // перед правкой — для Undo (key объединяет серию правок)
private:
    struct Row { QDoubleSpinBox* sp; std::function<double(double)> eval; };
    QWidget* makeRow(const QString& label, int fxIdx, const QString& key, double lo, double hi, double def);
    QMap<QString, double>& storeOf(int fxIdx);
    QMap<QString, KeyList>& keysOf(int fxIdx);
    ClipPtr clip;
    double now = 0;
    QWidget* body; QVBoxLayout* form; KeyframeGraph* graph;
    QVector<Row> rows;
};
