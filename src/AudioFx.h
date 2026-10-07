#pragma once
#include "Effects.h"
#include <memory>
#include <vector>

// Звуковые эффекты (DSP на CPU): EQ, фильтры, компрессор, лимитер, гейт, дилей, ревербератор, хорус, дисторшн…
struct AudioFxDef { QString name, category; QList<ParamDef> params; };
const QList<AudioFxDef>& audioFxDefs();
const AudioFxDef* findAudioFx(const QString& name);

class AudioFx {
public:
    virtual ~AudioFx() {}
    virtual void process(float* io, int frames, const double* p) = 0;     // io: стерео interleaved, 48 кГц
};
std::unique_ptr<AudioFx> makeAudioFx(const QString& name);

// цепочка эффектов со своим состоянием (линии задержки, фильтры); пересоздаётся при смене состава
class AudioChain {
public:
    void process(float* io, int frames, const std::vector<EffectInst>& fx, double localT);
private:
    QStringList names;
    std::vector<std::unique_ptr<AudioFx>> chain;
};

// амплитудный отклик 4-полосного EQ в дБ (для графика); p — параметры эффекта "EQ 4-band"
double eqResponseDb(const double* p, double freq, double sampleRate = 48000.0);
