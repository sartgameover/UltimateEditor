#include "Project.h"
#include "Decoder.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <algorithm>
#include <cmath>
#include "AppSettings.h"

bool isVideoTrack(const QString& t) { return t.startsWith('V'); }

double defaultProp(const QString& n) {
    if (n == "scale" || n == "opacity" || n == "volume") return 1.0;
    return 0.0;
}

QString mediaKind(const QString& path) {
    static const QStringList v = {"mp4","mov","mkv","avi","webm","m4v","mts","flv","wmv","mpg","mpeg","ts"};
    static const QStringList a = {"mp3","wav","flac","ogg","opus","m4a","aac","wma","aiff"};
    static const QStringList i = {"png","jpg","jpeg","bmp","webp","tif","tiff"};
    QString e = QFileInfo(path).suffix().toLower();
    if (v.contains(e)) return "video";
    if (a.contains(e)) return "audio";
    if (i.contains(e)) return "image";
    return "";
}

// ---------- keyframes ----------
double cubicBezierY(double x, double x1, double y1, double x2, double y2) {
    x = qBound(0.0, x, 1.0);
    auto bx = [&](double t) { double s = 1 - t; return 3 * s * s * t * x1 + 3 * s * t * t * x2 + t * t * t; };
    auto by = [&](double t) { double s = 1 - t; return 3 * s * s * t * y1 + 3 * s * t * t * y2 + t * t * t; };
    auto dbx = [&](double t) { double s = 1 - t; return 3 * s * s * x1 + 6 * s * t * (x2 - x1) + 3 * t * t * (1 - x2); };
    double t = x;
    for (int i = 0; i < 8; ++i) {
        double e = bx(t) - x;
        if (std::fabs(e) < 1e-6) break;
        double d = dbx(t);
        if (std::fabs(d) < 1e-6) break;
        t -= e / d;
    }
    if (t < 0 || t > 1 || std::fabs(bx(t) - x) > 1e-4) {              // запасной вариант — деление пополам
        double lo = 0, hi = 1; t = x;
        for (int i = 0; i < 30; ++i) { if (bx(t) < x) lo = t; else hi = t; t = (lo + hi) / 2; }
    }
    return by(t);
}

double evalKeys(const KeyList& k, double t, double def) {
    if (k.isEmpty()) return def;
    if (t <= k.first().t) return k.first().v;
    if (t >= k.last().t) return k.last().v;
    for (int i = 0; i + 1 < k.size(); ++i) {
        const Keyframe& a = k[i];
        const Keyframe& b = k[i + 1];
        if (t >= a.t && t <= b.t) {
            double u = (t - a.t) / qMax(b.t - a.t, 1e-9);
            if (a.mode == 2) return a.v;
            if (a.mode == 1) u = u * u * (3 - 2 * u);
            else if (a.mode == 3) u = cubicBezierY(u, a.c1x, a.c1y, a.c2x, a.c2y);
            return a.v + (b.v - a.v) * u;
        }
    }
    return def;
}

void setKey(KeyList& k, double t, double v, int mode) {
    for (int i = 0; i < k.size(); ++i) {
        if (qAbs(k[i].t - t) < 1e-3) { k[i].v = v; return; }
    }
    Keyframe f; f.t = t; f.v = v; f.mode = mode;
    k.append(f);
    std::sort(k.begin(), k.end(), [](const Keyframe& a, const Keyframe& b) { return a.t < b.t; });
}

double EffectInst::value(const QString& p, double t, double def) const {
    double base = params.value(p, def);
    auto it = keys.constFind(p);
    if (it != keys.constEnd() && !it.value().isEmpty()) return evalKeys(it.value(), t, base);
    return base;
}

double Clip::prop(const QString& n, double local) const {
    double base = props.value(n, defaultProp(n));
    auto it = keys.constFind(n);
    if (it != keys.constEnd() && !it.value().isEmpty()) return evalKeys(it.value(), local, base);
    return base;
}

double Clip::fadeFactor(double local) const {
    double f = 1.0;
    if (fadeIn > 1e-6) f *= qBound(0.0, local / fadeIn, 1.0);
    if (fadeOut > 1e-6) f *= qBound(0.0, (dur - local) / fadeOut, 1.0);
    return f;
}

// ---------- дорожки ----------
static int trackNum(const QString& n) { return n.mid(1).toInt(); }

void Project::normalizeTracks() {
    QStringList v, a;
    for (const QString& t : tracks) { if (isVideoTrack(t)) { if (!v.contains(t)) v << t; } else if (!a.contains(t)) a << t; }
    for (const ClipPtr& c : clips) {
        if (!c->track.isEmpty() && !tracks.contains(c->track)) { if (isVideoTrack(c->track)) { if (!v.contains(c->track)) v << c->track; } else if (!a.contains(c->track)) a << c->track; }
    }
    if (v.isEmpty()) v << "V1";
    if (a.isEmpty()) a << "A1";
    std::sort(v.begin(), v.end(), [](const QString& x, const QString& y) { return trackNum(x) > trackNum(y); });
    std::sort(a.begin(), a.end(), [](const QString& x, const QString& y) { return trackNum(x) < trackNum(y); });
    tracks = v + a;
}

QString Project::addTrack(bool video) {
    int mx = 0;
    for (const QString& t : tracks) if (isVideoTrack(t) == video) mx = qMax(mx, trackNum(t));
    QString name = QString(video ? "V%1" : "A%1").arg(mx + 1);
    tracks << name;
    normalizeTracks();
    return name;
}

bool Project::removeTrack(const QString& name) {
    for (const ClipPtr& c : clips) if (c->track == name) return false;
    int same = 0;
    for (const QString& t : tracks) if (isVideoTrack(t) == isVideoTrack(name)) ++same;
    if (same <= 1) return false;
    tracks.removeOne(name);
    return true;
}

QStringList Project::videoTracksBottomUp() const {
    QStringList r;
    for (int i = tracks.size() - 1; i >= 0; --i) if (isVideoTrack(tracks[i])) r << tracks[i];
    return r;
}

QStringList Project::audioTracks() const {
    QStringList r;
    for (const QString& t : tracks) if (!isVideoTrack(t)) r << t;
    return r;
}

// ---------- проект ----------
Asset* Project::addAsset(const QString& p) {
    for (auto it = assets.begin(); it != assets.end(); ++it)
        if (it.value().path == p) return &it.value();
    QString kind = mediaKind(p);
    if (kind.isEmpty()) return nullptr;
    Asset a; a.id = nextId++; a.path = p; a.kind = kind;
    if (kind == "image") {
        a.duration = AppSettings::imageDuration();
    } else {
        MediaInfo mi = probeMedia(p);
        a.duration = mi.duration; a.hasAudio = (kind == "audio") || mi.hasAudio; a.w = mi.w; a.h = mi.h;
    }
    assets[a.id] = a;
    return &assets[a.id];
}

QList<ClipPtr> Project::addClipsForAsset(const Asset& a, double t, const QString& trackIn) {
    QList<ClipPtr> out;
    double dur = a.kind == "image" ? AppSettings::imageDuration() : a.duration;
    QString track = trackIn;
    auto mk = [&](const QString& tr, const QString& kind, int link) {
        ClipPtr c = std::make_shared<Clip>();
        c->id = nextId++; c->assetId = a.id; c->track = tr; c->kind = kind;
        c->start = t; c->in = 0; c->dur = dur; c->link = link;
        clips.append(c); out.append(c);
    };
    if (a.kind == "audio") {
        if (isVideoTrack(track)) track = "A1";
        mk(track, "audio", 0);
    } else {
        if (!isVideoTrack(track)) track = "V1";
        int link = (a.kind == "video" && a.hasAudio) ? nextId++ : 0;
        mk(track, a.kind, link);
        if (link) mk("A1", "audio", link);
    }
    return out;
}

ClipPtr Project::addTextClip(const QString& text, double t, double dur) {
    ClipPtr c = std::make_shared<Clip>();
    c->id = nextId++; c->track = "V3"; c->kind = "text"; c->text = text; c->start = t; c->dur = dur;
    clips.append(c);
    return c;
}

QList<ClipPtr> Project::group(const ClipPtr& c) const {
    QList<ClipPtr> g;
    g.append(c);
    if (c->link) for (const ClipPtr& o : clips) if (o != c && o->link == c->link) g.append(o);
    return g;
}

QList<ClipPtr> Project::split(const ClipPtr& c, double t) {
    QList<ClipPtr> rights;
    int newLink = c->link ? nextId++ : 0;
    for (const ClipPtr& g : group(c)) {
        if (!(g->start + 0.01 < t && t < g->end() - 0.01)) continue;
        ClipPtr r = std::make_shared<Clip>(*g);
        double d = t - g->start, oldEnd = g->end();
        r->id = nextId++; r->start = t; r->in = g->in + d; r->dur = oldEnd - t; r->link = newLink;
        g->dur = d;
        rights.append(r);
    }
    clips.append(rights);
    return rights;
}

void Project::remove(const QList<ClipPtr>& cs) { for (const ClipPtr& c : cs) clips.removeOne(c); }

void Project::rippleDelete(const QList<ClipPtr>& cs) {
    for (const ClipPtr& c : cs) {
        for (const ClipPtr& g : group(c)) {
            if (!clips.contains(g)) continue;
            clips.removeOne(g);
            for (const ClipPtr& o : clips)
                if (o->track == g->track && o->start >= g->end() - 1e-6) o->start -= g->dur;
        }
    }
}

double Project::duration() const {
    double d = 0;
    for (const ClipPtr& c : clips) d = qMax(d, c->end());
    return d;
}

QList<ClipPtr> Project::clipsAt(double t) const {
    QList<ClipPtr> r;
    for (const ClipPtr& c : clips) if (c->start <= t && t < c->end()) r.append(c);
    return r;
}

double Project::trackGainEff(const QString& track) const {
    bool anySolo = false;
    for (auto it = solo.constBegin(); it != solo.constEnd(); ++it) if (it.value()) anySolo = true;
    if (mute.value(track, false)) return 0.0;
    if (anySolo && !solo.value(track, false)) return 0.0;
    return trackGain.value(track, 1.0);
}

double Project::gainFor(const QString& track) const { return trackGainEff(track) * master; }

// ---------- JSON ----------
static QJsonObject keysToJson(const QMap<QString, KeyList>& m) {
    QJsonObject o;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        QJsonArray a;
        for (const Keyframe& k : it.value()) a.append(QJsonArray{k.t, k.v, k.mode, k.c1x, k.c1y, k.c2x, k.c2y});
        o[it.key()] = a;
    }
    return o;
}
static QMap<QString, KeyList> keysFromJson(const QJsonObject& o) {
    QMap<QString, KeyList> m;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        KeyList l;
        for (const QJsonValue& v : it.value().toArray()) {
            QJsonArray a = v.toArray();
            Keyframe k; k.t = a.at(0).toDouble(); k.v = a.at(1).toDouble(); k.mode = a.at(2).toInt();
            if (a.size() >= 7) { k.c1x = a.at(3).toDouble(); k.c1y = a.at(4).toDouble(); k.c2x = a.at(5).toDouble(); k.c2y = a.at(6).toDouble(); }
            l.append(k);
        }
        m[it.key()] = l;
    }
    return m;
}
static QJsonObject numMapToJson(const QMap<QString, double>& m) {
    QJsonObject o;
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) o[it.key()] = it.value();
    return o;
}
static QMap<QString, double> numMapFromJson(const QJsonObject& o) {
    QMap<QString, double> m;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) m[it.key()] = it.value().toDouble();
    return m;
}

QJsonObject effectToJson(const EffectInst& e) {
    QJsonObject o; o["name"] = e.name; o["params"] = numMapToJson(e.params); o["keys"] = keysToJson(e.keys);
    return o;
}
EffectInst effectFromJson(const QJsonObject& o) {
    EffectInst e; e.name = o["name"].toString();
    e.params = numMapFromJson(o["params"].toObject()); e.keys = keysFromJson(o["keys"].toObject());
    return e;
}

static QJsonObject projectToObj(const Project& p) {
    QJsonObject root;
    root["notes"] = p.notes; root["bgColor"] = p.bgColor;
    root["fps"] = p.fps; root["W"] = p.W; root["H"] = p.H; root["nextId"] = p.nextId; root["master"] = p.master;
    QJsonArray tr; for (const QString& t : p.tracks) tr.append(t);
    root["tracks"] = tr;
    QJsonObject tg, mu, so;
    for (auto it = p.trackGain.constBegin(); it != p.trackGain.constEnd(); ++it) tg[it.key()] = it.value();
    for (auto it = p.mute.constBegin(); it != p.mute.constEnd(); ++it) mu[it.key()] = it.value();
    for (auto it = p.solo.constBegin(); it != p.solo.constEnd(); ++it) so[it.key()] = it.value();
    root["trackGain"] = tg; root["mute"] = mu; root["solo"] = so;
    QJsonObject tp, tfx;
    for (auto it = p.trackPan.constBegin(); it != p.trackPan.constEnd(); ++it) tp[it.key()] = it.value();
    for (auto it = p.trackFx.constBegin(); it != p.trackFx.constEnd(); ++it) {
        QJsonArray a; for (const EffectInst& e : it.value()) a.append(effectToJson(e));
        tfx[it.key()] = a;
    }
    root["trackPan"] = tp; root["trackFx"] = tfx;
    QJsonArray mfx; for (const EffectInst& e : p.masterFx) mfx.append(effectToJson(e));
    root["masterFx"] = mfx;
    QJsonArray mk; for (const Marker& m : p.markers) mk.append(QJsonArray{m.t, m.color});
    root["markers"] = mk;
    QJsonArray as;
    for (auto it = p.assets.constBegin(); it != p.assets.constEnd(); ++it) {
        const Asset& a = it.value(); QJsonObject o;
        o["id"] = a.id; o["path"] = a.path; o["kind"] = a.kind; o["proxy"] = a.proxy;
        o["duration"] = a.duration; o["hasAudio"] = a.hasAudio; o["w"] = a.w; o["h"] = a.h;
        as.append(o);
    }
    root["assets"] = as;
    QJsonArray cs;
    for (const ClipPtr& c : p.clips) {
        QJsonObject o;
        o["id"] = c->id; o["assetId"] = c->assetId; o["link"] = c->link; o["track"] = c->track; o["kind"] = c->kind;
        o["text"] = c->text; o["textColor"] = c->textColor; o["textSize"] = c->textSize; o["start"] = c->start; o["in"] = c->in; o["dur"] = c->dur;
        o["fadeIn"] = c->fadeIn; o["fadeOut"] = c->fadeOut;
        o["textFont"] = c->textFont; o["textBold"] = c->textBold; o["textItalic"] = c->textItalic; o["textShadow"] = c->textShadow;
        o["textOutline"] = c->textOutline; o["textOutlineColor"] = c->textOutlineColor; o["textAlign"] = c->textAlign;
        o["animInAngle"] = c->animInAngle; o["animOutAngle"] = c->animOutAngle; o["animLoopAngle"] = c->animLoopAngle;
        o["animInAmt"] = c->animInAmt; o["animOutAmt"] = c->animOutAmt; o["animLoopAmt"] = c->animLoopAmt;
        o["animInC"] = QJsonArray{c->animInC[0], c->animInC[1], c->animInC[2], c->animInC[3]};
        o["animOutC"] = QJsonArray{c->animOutC[0], c->animOutC[1], c->animOutC[2], c->animOutC[3]};
        o["animIn"] = c->animIn; o["animOut"] = c->animOut; o["animLoop"] = c->animLoop;
        o["animInDur"] = c->animInDur; o["animOutDur"] = c->animOutDur;
        o["props"] = numMapToJson(c->props); o["keys"] = keysToJson(c->keys);
        QJsonArray fx; for (const EffectInst& e : c->effects) fx.append(effectToJson(e));
        o["effects"] = fx;
        cs.append(o);
    }
    root["clips"] = cs;
    return root;
}

static bool projectFromObj(Project& p, const QJsonObject& root) {
    if (root.isEmpty()) return false;
    p.assets.clear(); p.clips.clear(); p.markers.clear(); p.trackGain.clear(); p.mute.clear(); p.solo.clear();
    p.notes = root["notes"].toString(); p.bgColor = root["bgColor"].toString("#000000");
    p.fps = root["fps"].toInt(30); p.W = root["W"].toInt(1280); p.H = root["H"].toInt(720);
    p.nextId = root["nextId"].toInt(1); p.master = root["master"].toDouble(1.0);
    p.tracks.clear();
    for (const QJsonValue& v : root["tracks"].toArray()) p.tracks << v.toString();
    QJsonObject tg = root["trackGain"].toObject(), mu = root["mute"].toObject(), so = root["solo"].toObject();
    for (auto it = tg.constBegin(); it != tg.constEnd(); ++it) p.trackGain[it.key()] = it.value().toDouble(1.0);
    for (auto it = mu.constBegin(); it != mu.constEnd(); ++it) p.mute[it.key()] = it.value().toBool();
    for (auto it = so.constBegin(); it != so.constEnd(); ++it) p.solo[it.key()] = it.value().toBool();
    p.trackPan.clear(); p.trackFx.clear(); p.masterFx.clear();
    QJsonObject tp = root["trackPan"].toObject(), tfx = root["trackFx"].toObject();
    for (auto it = tp.constBegin(); it != tp.constEnd(); ++it) p.trackPan[it.key()] = it.value().toDouble();
    for (auto it = tfx.constBegin(); it != tfx.constEnd(); ++it) {
        std::vector<EffectInst> v; for (const QJsonValue& e : it.value().toArray()) v.push_back(effectFromJson(e.toObject()));
        p.trackFx[it.key()] = v;
    }
    for (const QJsonValue& e : root["masterFx"].toArray()) p.masterFx.push_back(effectFromJson(e.toObject()));
    for (const QJsonValue& v : root["markers"].toArray()) {
        QJsonArray a = v.toArray(); Marker m; m.t = a.at(0).toDouble(); m.color = a.at(1).toString(); p.markers.append(m);
    }
    for (const QJsonValue& v : root["assets"].toArray()) {
        QJsonObject o = v.toObject(); Asset a;
        a.id = o["id"].toInt(); a.path = o["path"].toString(); a.kind = o["kind"].toString(); a.proxy = o["proxy"].toString();
        a.duration = o["duration"].toDouble(5); a.hasAudio = o["hasAudio"].toBool(); a.w = o["w"].toInt(); a.h = o["h"].toInt();
        p.assets[a.id] = a;
    }
    for (const QJsonValue& v : root["clips"].toArray()) {
        QJsonObject o = v.toObject(); ClipPtr c = std::make_shared<Clip>();
        c->id = o["id"].toInt(); c->assetId = o["assetId"].toInt(); c->link = o["link"].toInt();
        c->track = o["track"].toString(); c->kind = o["kind"].toString(); c->text = o["text"].toString();
        c->textColor = o["textColor"].toString("#ffffff"); c->textSize = o["textSize"].toDouble(1.0);
        c->textColor = o["textColor"].toString("#ffffff"); c->textSize = o["textSize"].toDouble(1.0);
        c->start = o["start"].toDouble(); c->in = o["in"].toDouble(); c->dur = o["dur"].toDouble();
        c->fadeIn = o["fadeIn"].toDouble(); c->fadeOut = o["fadeOut"].toDouble();
        c->textFont = o["textFont"].toString(); c->textBold = o["textBold"].toBool(true); c->textItalic = o["textItalic"].toBool(false);
        c->textShadow = o["textShadow"].toBool(false); c->textOutline = o["textOutline"].toDouble(2.0);
        c->textOutlineColor = o["textOutlineColor"].toString("#000000"); c->textAlign = o["textAlign"].toInt(1);
        c->animInAngle = o["animInAngle"].toDouble(); c->animOutAngle = o["animOutAngle"].toDouble(); c->animLoopAngle = o["animLoopAngle"].toDouble();
        c->animInAmt = o["animInAmt"].toDouble(1); c->animOutAmt = o["animOutAmt"].toDouble(1); c->animLoopAmt = o["animLoopAmt"].toDouble(1);
        { QJsonArray ia = o["animInC"].toArray(), oa = o["animOutC"].toArray();
          for (int i = 0; i < 4; ++i) { if (ia.size() == 4) c->animInC[i] = ia.at(i).toDouble(); if (oa.size() == 4) c->animOutC[i] = oa.at(i).toDouble(); } }
        c->animIn = o["animIn"].toString(); c->animOut = o["animOut"].toString(); c->animLoop = o["animLoop"].toString();
        c->animInDur = o["animInDur"].toDouble(0.8); c->animOutDur = o["animOutDur"].toDouble(0.8);
        c->props = numMapFromJson(o["props"].toObject()); c->keys = keysFromJson(o["keys"].toObject());
        for (const QJsonValue& e : o["effects"].toArray()) c->effects.push_back(effectFromJson(e.toObject()));
        p.clips.append(c);
    }
    p.normalizeTracks();
    return true;
}

QByteArray Project::toBytes() const { return QJsonDocument(projectToObj(*this)).toJson(QJsonDocument::Compact); }

bool Project::fromBytes(const QByteArray& b) { return projectFromObj(*this, QJsonDocument::fromJson(b).object()); }

// Формат .uvmvideos: сигнатура "UVMV1" + сжатый (zlib) JSON. Старые чистые JSON-проекты тоже читаются.
bool Project::save(const QString& file) const {
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write("UVMV1");
    f.write(qCompress(QJsonDocument(projectToObj(*this)).toJson(QJsonDocument::Compact), 9));
    return true;
}

bool Project::load(const QString& file) {
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray all = f.readAll();
    QByteArray json = all;
    if (all.startsWith("UVMV1")) json = qUncompress(all.mid(5));
    if (!projectFromObj(*this, QJsonDocument::fromJson(json).object())) return false;
    path = file;
    return true;
}
