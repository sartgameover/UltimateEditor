#pragma once
#include <QObject>
#include <QAudioFormat>
#include <QFile>

class QAudioSource; class QAudioDevice; class QIODevice;

// Запись голоса с выбранного микрофона в WAV (16 бит). Громкость записи (gain) меняется на лету.
class VoiceRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr) : QObject(parent) {}
    bool start(const QAudioDevice& dev, const QString& path);
    void stop();
    bool isRecording() const { return src != nullptr; }
    void setGain(double g) { gain = g; }
signals:
    void level(float l);                          // 0..1
    void finished(QString path, double seconds);
    void failed(QString message);
private slots:
    void onReady();
private:
    QAudioSource* src = nullptr;
    QIODevice* io = nullptr;
    QFile file;
    QAudioFormat fmt;
    double gain = 1.0;
    qint64 bytes = 0;
};
