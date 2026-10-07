#include "AudioEngine.h"
#include "AudioFx.h"
#include "Decoder.h"
#include <QAudioSink>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QIODevice>
#include <QFile>
#include <QMetaObject>
#include <map>
#include <mutex>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

std::shared_ptr<ASnapshot> makeAudioSnapshot(const Project& p) {
    auto s = std::make_shared<ASnapshot>();
    for (const QString& t : p.audioTracks()) {
        ATrack tr;
        tr.gain = p.trackGainEff(t);
        tr.pan = p.trackPan.value(t, 0.0);
        tr.fx = p.trackFx.value(t);
        s->tracks[t] = tr;
    }
    s->master.gain = p.master;
    s->master.fx = p.masterFx;
    for (const ClipPtr& c : p.clips) {
        if (c->kind != "audio") continue;
        auto ai = p.assets.constFind(c->assetId);
        if (ai == p.assets.constEnd()) continue;
        AClip a;
        a.id = c->id; a.path = ai.value().path; a.track = c->track;
        a.start = c->start; a.in = c->in; a.dur = c->dur; a.fadeIn = c->fadeIn; a.fadeOut = c->fadeOut;
        a.volume = c->prop("volume", 0);
        a.fx = c->effects;
        s->clips.push_back(a);
    }
    return s;
}

// ====================== ядро микшера ======================
class AudioMixerCore {
public:
    void setSnapshot(const std::shared_ptr<const ASnapshot>& s) { if (s != snap) snap = s; }
    double position() const { return pos; }
    void seek(double t) { pos = std::max(t, 0.0); }
    QMap<QString, float> levels;
    void render(float* out, int frames);
private:
    std::shared_ptr<const ASnapshot> snap;
    double pos = 0;
    std::map<int, std::unique_ptr<AudioDecoder>> decs;
    std::map<int, bool> failed;
    std::map<int, AudioChain> clipChains;
    std::map<QString, AudioChain> trackChains;
    AudioChain masterChain;
    std::vector<float> cbuf;
    std::map<QString, std::vector<float>> tbufs;
};

void AudioMixerCore::render(float* out, int frames) {
    const double sr = 48000.0;
    std::fill(out, out + (size_t)frames * 2, 0.f);
    if (!snap) { pos += frames / sr; return; }
    const double t0 = pos, t1 = pos + frames / sr;
    for (auto it = snap->tracks.constBegin(); it != snap->tracks.constEnd(); ++it) tbufs[it.key()].assign((size_t)frames * 2, 0.f);
    for (const AClip& c : snap->clips) {
        double cs = c.start, ce = c.start + c.dur;
        if (ce <= t0 || cs >= t1) continue;
        int sf = (int)std::max(0.0, std::ceil((cs - t0) * sr));
        int ef = (int)std::min<double>(frames, std::floor((ce - t0) * sr));
        int n = ef - sf;
        if (n <= 0) continue;
        auto tb = tbufs.find(c.track);
        if (tb == tbufs.end()) continue;
        auto& dec = decs[c.id];
        if (!dec) {
            if (failed.count(c.id)) { decs.erase(c.id); continue; }
            dec.reset(new AudioDecoder);
            if (!dec->open(c.path)) { decs.erase(c.id); failed[c.id] = true; continue; }
        }
        cbuf.assign((size_t)n * 2, 0.f);
        double local0 = t0 + sf / sr - cs;
        dec->read(c.in + local0, cbuf.data(), n);
        for (int i = 0; i < n; ++i) {                                   // громкость клипа + затухание
            double local = local0 + i / sr;
            double f = c.volume;
            if (c.fadeIn > 1e-6) f *= std::min(std::max(local / c.fadeIn, 0.0), 1.0);
            if (c.fadeOut > 1e-6) f *= std::min(std::max((c.dur - local) / c.fadeOut, 0.0), 1.0);
            cbuf[2 * i] *= (float)f; cbuf[2 * i + 1] *= (float)f;
        }
        if (!c.fx.empty()) clipChains[c.id].process(cbuf.data(), n, c.fx, local0);
        float* dst = tb->second.data() + (size_t)sf * 2;
        for (int i = 0; i < n * 2; ++i) dst[i] += cbuf[i];
    }
    QMap<QString, float> lv;
    for (auto it = snap->tracks.constBegin(); it != snap->tracks.constEnd(); ++it) {
        std::vector<float>& b = tbufs[it.key()];
        const ATrack& tr = it.value();
        if (!tr.fx.empty()) trackChains[it.key()].process(b.data(), frames, tr.fx, t0);
        double th = (tr.pan + 1.0) * M_PI / 4.0;
        float gl = (float)(tr.gain * std::cos(th) * M_SQRT2), gr = (float)(tr.gain * std::sin(th) * M_SQRT2), pk = 0.f;
        for (int i = 0; i < frames; ++i) {
            float l = b[2 * i] * gl, r = b[2 * i + 1] * gr;
            pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
            out[2 * i] += l; out[2 * i + 1] += r;
        }
        lv[it.key()] = pk;
    }
    if (!snap->master.fx.empty()) masterChain.process(out, frames, snap->master.fx, t0);
    float mg = (float)snap->master.gain, mpk = 0.f;
    for (int i = 0; i < frames * 2; ++i) {
        float v = std::min(std::max(out[i] * mg, -1.0f), 1.0f);
        out[i] = v; mpk = std::max(mpk, std::fabs(v));
    }
    lv["Master"] = mpk;
    levels = lv;
    pos = t1;
}

// ====================== устройство для QAudioSink ======================
class MixDevice : public QIODevice {
public:
    double latency = 0.08;
    void publish(const std::shared_ptr<const ASnapshot>& s, double t, bool pl) {
        std::lock_guard<std::mutex> g(mx);
        if (s) snap = s;
        clockT = t; clockAt = std::chrono::steady_clock::now(); playing = pl;
    }
    QMap<QString, float> levelsCopy() { std::lock_guard<std::mutex> g(lmx); return lv; }
    qint64 readData(char* data, qint64 maxlen) override {
        int frames = (int)(maxlen / 4);
        int16_t* o = reinterpret_cast<int16_t*>(data);
        std::shared_ptr<const ASnapshot> s; double ct; std::chrono::steady_clock::time_point cat; bool pl;
        { std::lock_guard<std::mutex> g(mx); s = snap; ct = clockT; cat = clockAt; pl = playing; }
        if (!pl) { std::memset(data, 0, (size_t)frames * 4); return (qint64)frames * 4; }
        double expected = ct + std::chrono::duration<double>(std::chrono::steady_clock::now() - cat).count() + latency;
        core.setSnapshot(s);
        if (std::fabs(core.position() - expected) > 0.12) core.seek(expected);   // держим синхрон с видео
        int done = 0;
        while (done < frames) {
            int n = std::min(1024, frames - done);
            fb.assign((size_t)n * 2, 0.f);
            core.render(fb.data(), n);
            for (int i = 0; i < n * 2; ++i) o[(size_t)done * 2 + i] = (int16_t)std::lrint(fb[i] * 32767.0f);
            done += n;
        }
        { std::lock_guard<std::mutex> g(lmx); lv = core.levels; }
        return (qint64)frames * 4;
    }
    qint64 writeData(const char*, qint64) override { return 0; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 16; }
private:
    AudioMixerCore core;
    std::mutex mx, lmx;
    std::shared_ptr<const ASnapshot> snap;
    double clockT = 0;
    std::chrono::steady_clock::time_point clockAt = std::chrono::steady_clock::now();
    bool playing = false;
    QMap<QString, float> lv;
    std::vector<float> fb;
};

class AudioOutput : public QObject {
    Q_OBJECT
public:
    explicit AudioOutput(MixDevice* d) : dev(d) {}
public slots:
    void init() {
        QAudioFormat f;
        f.setSampleRate(48000); f.setChannelCount(2); f.setSampleFormat(QAudioFormat::Int16);
        QAudioDevice d = QMediaDevices::defaultAudioOutput();
        sink = new QAudioSink(d, f, this);
        sink->setBufferSize((int)f.bytesForDuration(80000));
        dev->latency = 0.08;
        sink->start(dev);
    }
    void shutdown() {
        if (sink) { sink->stop(); delete sink; sink = nullptr; }
    }
private:
    MixDevice* dev;
    QAudioSink* sink = nullptr;
};

AudioEngine::AudioEngine(Project* p) : project(p) {
    if (qEnvironmentVariableIsSet("UVE_NO_AUDIO")) return;            // безопасный режим: без звукового движка
    dev = new MixDevice;
    dev->open(QIODevice::ReadOnly);
    out = new AudioOutput(dev);
    out->moveToThread(&thread);
    dev->moveToThread(&thread);
    thread.start();
    QMetaObject::invokeMethod(out, "init", Qt::QueuedConnection);
}

AudioEngine::~AudioEngine() {
    if (!dev) return;
    QMetaObject::invokeMethod(out, "shutdown", Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    delete out;
    delete dev;
}

void AudioEngine::sync(double t, bool playing) {
    if (!dev) return;
    bool trans = (playing != wasPlaying);
    wasPlaying = playing;
    if (!playing && !trans) return;
    if (playing && !trans && sinceSnap.isValid() && sinceSnap.elapsed() < 50) return;     // обновляем ~20 раз в секунду
    if (playing) dev->publish(makeAudioSnapshot(*project), t, true);
    else dev->publish(std::shared_ptr<const ASnapshot>(), t, false);
    sinceSnap.restart();
}

QMap<QString, float> AudioEngine::levels() const { return dev ? dev->levelsCopy() : QMap<QString, float>(); }

// ====================== оффлайн-рендер для экспорта ======================
static void put32(QByteArray& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.append((char)((v >> (8 * i)) & 0xFF)); }
static void put16(QByteArray& b, uint16_t v) { for (int i = 0; i < 2; ++i) b.append((char)((v >> (8 * i)) & 0xFF)); }

bool renderMixToWav(const Project& p, const QString& path) {
    std::shared_ptr<const ASnapshot> s = makeAudioSnapshot(p);
    if (s->clips.empty()) return false;
    AudioMixerCore core;
    core.setSnapshot(s);
    core.seek(0);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QByteArray(44, '\0'));
    qint64 total = (qint64)(p.duration() * 48000.0), done = 0, bytes = 0;
    std::vector<float> fb;
    QByteArray ob;
    while (done < total) {
        int n = (int)std::min<qint64>(4096, total - done);
        fb.assign((size_t)n * 2, 0.f);
        core.render(fb.data(), n);
        ob.resize(n * 4);
        int16_t* o = reinterpret_cast<int16_t*>(ob.data());
        for (int i = 0; i < n * 2; ++i) o[i] = (int16_t)std::lrint(fb[i] * 32767.0f);
        f.write(ob);
        bytes += ob.size(); done += n;
    }
    QByteArray h;
    h.append("RIFF"); put32(h, (uint32_t)(36 + bytes)); h.append("WAVE");
    h.append("fmt "); put32(h, 16); put16(h, 1); put16(h, 2); put32(h, 48000);
    put32(h, 48000 * 4); put16(h, 4); put16(h, 16);
    h.append("data"); put32(h, (uint32_t)bytes);
    f.seek(0); f.write(h); f.close();
    return true;
}

#include "AudioEngine.moc"
