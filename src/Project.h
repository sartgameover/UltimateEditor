#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QVector>
#include <QJsonObject>
#include <memory>
#include <vector>

// ---------- ключевые кадры ----------
// mode: 0 linear, 1 ease (smoothstep), 2 hold, 3 cubic-bezier(c1x,c1y,c2x,c2y) — гибкая кривая как в After Effects / CSS
struct Keyframe { double t = 0, v = 0; int mode = 0; double c1x = 0.42, c1y = 0, c2x = 0.58, c2y = 1; };
typedef QVector<Keyframe> KeyList;
double cubicBezierY(double x, double c1x, double c1y, double c2x, double c2y);   // кривая плавности: x∈[0,1] -> y
double evalKeys(const KeyList& k, double t, double def);
void setKey(KeyList& k, double t, double v, int mode = 0);

struct EffectInst {
    QString name;
    QMap<QString, double> params;
    QMap<QString, KeyList> keys;
    double value(const QString& p, double t, double def) const;
};
QJsonObject effectToJson(const EffectInst& e);
EffectInst effectFromJson(const QJsonObject& o);

// ---------- клип ----------
struct Clip {
    int id = 0, assetId = 0, link = 0;
    QString track, kind, text;            // kind: video | audio | image | text
    double start = 0, in = 0, dur = 0;
    QMap<QString, double> props;          // x y scale rot opacity volume
    QMap<QString, KeyList> keys;
    std::vector<EffectInst> effects;
    QString textColor = "#ffffff";                     // стиль текста
    double textSize = 1.0;
    QString textFont;                                  // "" — по умолчанию, "UVF:Имя" — свой штриховой шрифт
    bool textBold = true, textItalic = false, textShadow = false;
    double textOutline = 2.0;
    QString textOutlineColor = "#000000";
    int textAlign = 1;                                 // 0 слева, 1 по центру, 2 справа
    double fadeIn = 0, fadeOut = 0;                    // затухание в начале/конце (сек): картинка или звук
    QString animIn, animOut, animLoop;                 // анимации (см. Effects.h)
    double animInDur = 0.8, animOutDur = 0.8;
    // настройки анимаций: угол (направление или градусы поворота 0..360), сила, кривая плавности (cubic-bezier)
    double animInAngle = 0, animOutAngle = 0, animLoopAngle = 0;
    double animInAmt = 1, animOutAmt = 1, animLoopAmt = 1;
    double animInC[4] = {0.42, 0, 0.58, 1};
    double animOutC[4] = {0.42, 0, 0.58, 1};
    double end() const { return start + dur; }
    double prop(const QString& n, double local) const;
    double fadeFactor(double local) const;
};
typedef std::shared_ptr<Clip> ClipPtr;

struct Asset { int id = 0; QString path, kind, proxy; double duration = 5; bool hasAudio = false; int w = 0, h = 0; };
struct Marker { double t = 0; QString color; };

bool isVideoTrack(const QString& t);
double defaultProp(const QString& n);
QString mediaKind(const QString& path);   // video | audio | image | ""

class Project {
public:
    Project() { tracks = QStringList() << "V3" << "V2" << "V1" << "A1" << "A2" << "A3"; }
    QStringList tracks;                                        // сверху вниз
    QString addTrack(bool video);                              // возвращает имя новой дорожки
    bool removeTrack(const QString& name);                     // только пустую и не последнюю своего типа
    QStringList videoTracksBottomUp() const;
    QStringList audioTracks() const;
    void normalizeTracks();
    QByteArray toBytes() const;                                // снимок для Undo
    bool fromBytes(const QByteArray& b);
    QMap<int, Asset> assets;
    QList<ClipPtr> clips;
    QList<Marker> markers;
    int fps = 30, W = 1280, H = 720, nextId = 1;
    QString path;
    QString notes;                                             // заметки к проекту
    QString bgColor = "#000000";                               // фон кадра
    // микшер
    QMap<QString, double> trackGain;
    QMap<QString, bool> mute, solo;
    double master = 1.0;
    QMap<QString, double> trackPan;                            // -1..1
    QMap<QString, std::vector<EffectInst>> trackFx;            // вставки (EQ, компрессор…) на канале
    std::vector<EffectInst> masterFx;
    double trackGainEff(const QString& track) const;           // фейдер с учётом Mute/Solo, без master

    Asset* addAsset(const QString& path);                       // nullptr — формат не поддерживается
    QList<ClipPtr> addClipsForAsset(const Asset& a, double t, const QString& track);
    ClipPtr addTextClip(const QString& text, double t, double dur);
    QList<ClipPtr> split(const ClipPtr& c, double t);           // лезвие (режет и связанные клипы)
    void rippleDelete(const QList<ClipPtr>& cs);
    void remove(const QList<ClipPtr>& cs);
    QList<ClipPtr> group(const ClipPtr& c) const;               // клип + связанные с ним
    double duration() const;
    QList<ClipPtr> clipsAt(double t) const;
    double gainFor(const QString& track) const;                 // фейдер дорожки × master с учётом Mute/Solo
    bool save(const QString& file) const;
    bool load(const QString& file);
};
