#pragma once
#include "Project.h"
#include <QDialog>

class PlayerView; class QLineEdit; class QComboBox; class QPushButton; class QCheckBox; class QLabel; class WaterProgress;

// Окно рендера: папка, имя, формат (MP4/MOV/MKV/AVI/WebM или аудио), кодек, CPU/NVENC/VAAPI, FPS.
// Кадры считает тот же GPU-компоновщик, что и плеер; в ffmpeg уходит сырой RGBA, звук микшируется с учётом фейдеров.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    ExportDialog(Project* p, PlayerView* player, QWidget* parent = nullptr);
private slots:
    void kindChanged();
    void start();
private:
    Project* project; PlayerView* player;
    QComboBox* dir; QLineEdit* name; QComboBox* kind; QComboBox* cont; QComboBox* codec; QComboBox* enc; QComboBox* fps;
    WaterProgress* water; QLabel* status; QCheckBox* openAfter; QPushButton* go; bool cancel = false, running = false;
protected:
    void reject() override;
};
