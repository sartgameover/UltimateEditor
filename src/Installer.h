#pragma once
#include <QDialog>
#include <QPixmap>
#include <QTimer>

class QStackedWidget; class QCheckBox; class QLabel; class WaterProgress; class QPushButton;

// Красивый установщик, который показывает AppImage при первом запуске: «Установить» или «Запустить без установки».
// Ничего не требует от терминала: копирует AppImage в ~/.local/opt, создаёт ярлыки, значок и ассоциацию .uvmvideos.
class InstallerDialog : public QDialog {
    Q_OBJECT
public:
    InstallerDialog();
    static bool shouldRun(const QStringList& args);        // true: запущено из AppImage и ещё не установлено
    static bool uninstall();                               // --uninstall
    bool installedNow() const { return didInstall; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void startInstall();
    void step();
    QStackedWidget* pages;
    QCheckBox* menuIcon; QCheckBox* desktopIcon; QCheckBox* assoc;
    WaterProgress* water; QLabel* stepLabel; QPushButton* launch;
    QTimer timer;
    int stepNo = 0;
    bool didInstall = false;
    QPixmap banner, icon;
    QString installedExe;
};
