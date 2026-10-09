#pragma once
#include <QDialog>
#include <QAudioDevice>
#include <QList>
#include <QMap>
#include <QColor>

class QListWidget; class QStackedWidget; class QComboBox; class QSpinBox; class QDoubleSpinBox; class QCheckBox;
class QPushButton; class QTableWidget; class QAction; class QFontComboBox; class QKeySequenceEdit; class QFormLayout;

// Настройки всей программы: общие, тема (тёмная / светлая / своя), таймлайн, плеер, звук, производительность,
// горячие клавиши, пути, сброс.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr, const QList<QAction*>& actions = QList<QAction*>());
    static QAudioDevice selectedMic();           // микрофон из настроек (или системный по умолчанию)
    static int autosaveSeconds();
private:
    void save();
    void refreshColorButtons();
    QWidget* makePage(const QString& title, QFormLayout*& form);
    QListWidget* nav; QStackedWidget* pages;
    // общие
    QSpinBox* autosave; QSpinBox* undoLimit; QCheckBox* startScreen; QDoubleSpinBox* imageSec;
    // тема
    QComboBox* themeBox; QFontComboBox* uiFont; QSpinBox* uiFontSize;
    QMap<QString, QPushButton*> colorBtns; QMap<QString, QColor> custom;
    // таймлайн / плеер
    QSpinBox* trackH; QSpinBox* snapPx; QCheckBox* waves; QCheckBox* loop; QCheckBox* grid; QCheckBox* safe;
    // звук
    QComboBox* outDev; QComboBox* micDev; QSpinBox* latency;
    // производительность
    QSpinBox* proxyH;
    // горячие клавиши
    QList<QAction*> acts; QList<QKeySequenceEdit*> keyEdits;
};
