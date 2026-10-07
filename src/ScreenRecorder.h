#pragma once
#include <QObject>
#include <QWidget>
#include <QMediaCaptureSession>
#include <QScreenCapture>
#include <QMediaRecorder>
#include <QAudioDevice>
#include <QTimer>
#include <QElapsedTimer>

class QAudioInput; class QLabel;

// Запись экрана (Qt Multimedia: X11 и Wayland через PipeWire) + при желании звук с микрофона.
class ScreenRecorder : public QObject {
    Q_OBJECT
public:
    explicit ScreenRecorder(QObject* parent = nullptr);
    bool start(const QString& path, bool withMic, const QAudioDevice& mic);
    void stop();
    bool isRecording() const { return rec; }
signals:
    void finished(QString path);
    void failed(QString message);
private:
    QMediaCaptureSession session;
    QScreenCapture capture;
    QMediaRecorder recorder;
    QAudioInput* audioIn = nullptr;
    bool rec = false, stopping = false;
};

// Маленькая плавающая панель «● REC 00:12  [■ Стоп]» поверх всех окон
class RecordHud : public QWidget {
    Q_OBJECT
public:
    RecordHud();
    void begin();
signals:
    void stopClicked();
private:
    QLabel* label;
    QTimer timer;
    QElapsedTimer clock;
};
