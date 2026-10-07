#pragma once
#include <QDialog>
#include <QPixmap>
#include <QPoint>

class QListWidget; class QListWidgetItem;

// Стартовое мини-окно вместо редактора: фон — Image/Baner, слева значок BanrIcon, справа два столбца:
// «Недавние» и «Найдено на компьютере» (*.uvmvideos).
class StartScreen : public QDialog {
    Q_OBJECT
public:
    StartScreen();
    QString chosenPath() const { return chosen; }          // пусто — новый проект
public slots:
    void addFound(const QString& path);
    void scanFinished();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    void pick(QListWidgetItem* it);
    QListWidget* recent; QListWidget* found;
    QPixmap banner, icon;
    QString chosen;
    QPoint dragOff;
};
