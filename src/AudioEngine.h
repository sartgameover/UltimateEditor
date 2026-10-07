#pragma once
#include "Project.h"
#include <QObject>
#include <QThread>
#include <QMap>
#include <QElapsedTimer>
#include <memory>
#include <vector>

// Снимок звуковой части проекта для аудио-потока (копия, чтобы не было гонок с GUI)
struct AClip { int id = 0; QString path, track; double start = 0, in = 0, dur = 0, fadeIn = 0, fadeOut = 0, volume = 1; std::vector<EffectInst> fx; };
struct ATrack { double gain = 1, pan = 0; std::vector<EffectInst> fx; };
struct ASnapshot { std::vector<AClip> clips; QMap<QString, ATrack> tracks; ATrack master; };
std::shared_ptr<ASnapshot> makeAudioSnapshot(const Project& p);

// Оффлайн-рендер звука всего проекта в WAV (тот же микшер и те же эффекты, что и в плеере) — для экспорта
bool renderMixToWav(const Project& p, const QString& wavPath);

class MixDevice;
class AudioOutput;

// Собственный аудио-движок: декодирование (libav) -> клипы -> эффекты клипа -> канал (EQ/компрессор/pan/fader) -> master -> QAudioSink.
// Работает в отдельном потоке, поэтому видео-декодирование и интерфейс не вызывают треск.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(Project* p);
    ~AudioEngine() override;
    void setProject(Project* p) { stop(); project = p; }
    void sync(double t, bool playing);
    void stop() { sync(0, false); }
    QMap<QString, float> levels() const;                // пиковые уровни каналов (и "Master")
private:
    Project* project;
    MixDevice* dev = nullptr;
    AudioOutput* out = nullptr;
    QThread thread;
    bool wasPlaying = false;
    QElapsedTimer sinceSnap;
};
