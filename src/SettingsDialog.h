#pragma once
#include <QDialog>
#include <QAudioDevice>

class QComboBox; class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    static QAudioDevice selectedMic();           // микрофон из настроек (или системный по умолчанию)
    static int autosaveSeconds();
private:
    void save();
    QComboBox* mics; QSpinBox* autosave;
};
