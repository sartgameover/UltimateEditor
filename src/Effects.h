#pragma once
#include "Project.h"
#include <QList>
#include <QString>

struct ParamDef { QString name; double def, lo, hi; };
struct EffectDef {
    QString name, category, body;      // body — GLSL: vec4 fx(vec2 uv); параметры p0..p8 по порядку params
    QList<ParamDef> params;
};

const QList<EffectDef>& effectDefs();
const EffectDef* findEffect(const QString& name);
QString effectFragmentSource(const EffectDef& d);
extern const char* kVertexSource;

// общий поиск параметров (видео- или аудио-эффект) — для инспектора
const QList<ParamDef>* effectParams(const QString& name);
bool isAudioEffect(const QString& name);

// пользовательские пресеты
QStringList presetNames();
std::vector<EffectInst> loadPreset(const QString& name);
void savePreset(const QString& name, const std::vector<EffectInst>& fx);
void deletePreset(const QString& name);

// анимации: вход / выход / постоянные; длительность входа и выхода тянется «шариками» на клипе
struct AnimMod { double dx = 0, dy = 0, scale = 1, rot = 0, opacity = 1, sx = 1, sy = 1; };
QStringList animationInNames();
QStringList animationOutNames();
QStringList animationLoopNames();
void setAnimation(Clip& c, const QString& name);              // сама определяет слот (вход/выход/цикл) и ставит стартовые угол/силу
QString animationSlot(const QString& name);                    // "in" | "out" | "loop" | ""
AnimMod animationMod(const Clip& c, double local);
