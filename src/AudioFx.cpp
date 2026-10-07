#include "AudioFx.h"
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ================= реестр =================
static QList<AudioFxDef> buildAudio() {
    QList<AudioFxDef> L;
    auto add = [&](const char* name, const char* cat, QList<ParamDef> params) {
        AudioFxDef d; d.name = name; d.category = cat; d.params = params; L.append(d);
    };
    add("Gain", "Звук: громкость и динамика", {{"gain_db", 0, -30, 30}});
    add("Pan / Balance", "Звук: громкость и динамика", {{"pan", 0, -1, 1}});
    add("Stereo Width", "Звук: пространство", {{"width", 1, 0, 2}});
    add("Compressor", "Звук: громкость и динамика",
        {{"threshold_db", -18, -60, 0}, {"ratio", 3, 1, 20}, {"attack_ms", 10, 0.1, 200}, {"release_ms", 120, 10, 1000}, {"makeup_db", 0, 0, 24}});
    add("Limiter", "Звук: громкость и динамика", {{"ceiling_db", -1, -20, 0}, {"release_ms", 80, 5, 500}});
    add("Noise Gate", "Звук: громкость и динамика", {{"threshold_db", -50, -90, 0}, {"attack_ms", 5, 0.1, 100}, {"release_ms", 100, 10, 1000}});
    add("EQ 4-band", "Звук: EQ и фильтры",
        {{"low_gain", 0, -18, 18}, {"low_freq", 100, 20, 400}, {"m1_gain", 0, -18, 18}, {"m1_freq", 500, 100, 2000}, {"m1_q", 1, 0.2, 8},
         {"m2_gain", 0, -18, 18}, {"m2_freq", 3000, 1000, 10000}, {"m2_q", 1, 0.2, 8}, {"high_gain", 0, -18, 18}, {"high_freq", 8000, 2000, 18000}});
    add("High-pass Filter", "Звук: EQ и фильтры", {{"freq", 80, 20, 2000}});
    add("Low-pass Filter", "Звук: EQ и фильтры", {{"freq", 12000, 500, 20000}});
    add("Delay / Echo", "Звук: пространство", {{"time_ms", 350, 10, 1500}, {"feedback", 0.35, 0, 0.95}, {"mix", 0.3, 0, 1}});
    add("Reverb", "Звук: пространство", {{"room", 0.6, 0, 1}, {"damping", 0.4, 0, 1}, {"mix", 0.25, 0, 1}});
    add("Chorus", "Звук: пространство", {{"rate_hz", 0.8, 0.1, 5}, {"depth_ms", 6, 0.5, 15}, {"mix", 0.4, 0, 1}});
    add("Distortion", "Звук: громкость и динамика", {{"drive", 6, 1, 40}, {"mix", 1, 0, 1}});
    add("Bass & Treble", "Звук: EQ и фильтры", {{"bass_db", 0, -12, 12}, {"treble_db", 0, -12, 12}});
    add("Flange", "Звук: пространство", {{"rate_hz", 0.3, 0.05, 3}, {"depth_ms", 3, 0.3, 8}, {"feedback", 0.4, 0, 0.9}, {"mix", 0.5, 0, 1}});
    add("Modulator", "Звук: пространство", {{"rate_hz", 5, 0.2, 40}, {"depth", 0.5, 0, 1}, {"ring", 0, 0, 1}});
    add("Stereo Mixer", "Звук: пространство", {{"left_level", 1, 0, 2}, {"right_level", 1, 0, 2}, {"left_pan", -1, -1, 1}, {"right_pan", 1, -1, 1}});
    return L;
}
const QList<AudioFxDef>& audioFxDefs() { static QList<AudioFxDef> L = buildAudio(); return L; }
const AudioFxDef* findAudioFx(const QString& name) {
    for (const AudioFxDef& d : audioFxDefs()) if (d.name == name) return &d;
    return nullptr;
}

// общий реестр для инспектора (видео или аудио)
const QList<ParamDef>* effectParams(const QString& name) {
    if (const EffectDef* d = findEffect(name)) return &d->params;
    if (const AudioFxDef* a = findAudioFx(name)) return &a->params;
    return nullptr;
}
bool isAudioEffect(const QString& name) { return findAudioFx(name) != nullptr; }

// ================= DSP =================
static const double SR = 48000.0;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = {0, 0}, z2[2] = {0, 0};
    enum Type { Peak, LowShelf, HighShelf, LowPass, HighPass };
    void set(Type t, double fs, double f, double q, double gdb) {
        f = std::min(std::max(f, 10.0), fs * 0.45);
        double A = std::pow(10.0, gdb / 40.0), w = 2 * M_PI * f / fs, cs = std::cos(w), sn = std::sin(w);
        double al = sn / (2 * std::max(q, 0.05)), a0 = 1;
        double sq = 2 * std::sqrt(A) * (sn / 2 * std::sqrt(2.0));
        switch (t) {
            case Peak:
                b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; break;
            case LowShelf:
                b0 = A * ((A + 1) - (A - 1) * cs + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - sq);
                a0 = (A + 1) + (A - 1) * cs + sq; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - sq; break;
            case HighShelf:
                b0 = A * ((A + 1) + (A - 1) * cs + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sq);
                a0 = (A + 1) - (A - 1) * cs + sq; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - sq; break;
            case LowPass:
                b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
            case HighPass:
                b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        }
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    inline float tick(int ch, float x) {
        double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float)y;
    }
    double magDb(double fs, double f) const {
        double w = 2 * M_PI * f / fs;
        std::complex<double> e1 = std::polar(1.0, -w), e2 = std::polar(1.0, -2 * w);
        std::complex<double> h = (b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2);
        return 20 * std::log10(std::max(std::abs(h), 1e-9));
    }
};

static void eqSetup(Biquad* b, const double* p, double fs) {
    b[0].set(Biquad::LowShelf, fs, p[1], 0.7, p[0]);
    b[1].set(Biquad::Peak, fs, p[3], p[4], p[2]);
    b[2].set(Biquad::Peak, fs, p[6], p[7], p[5]);
    b[3].set(Biquad::HighShelf, fs, p[9], 0.7, p[8]);
}

double eqResponseDb(const double* p, double freq, double fs) {
    Biquad b[4]; eqSetup(b, p, fs);
    double s = 0; for (int i = 0; i < 4; ++i) s += b[i].magDb(fs, freq);
    return s;
}

static inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }

struct GainFx : AudioFx {
    void process(float* io, int n, const double* p) override { float g = (float)dbToLin(p[0]); for (int i = 0; i < n * 2; ++i) io[i] *= g; }
};
struct PanFx : AudioFx {
    void process(float* io, int n, const double* p) override {
        double th = (p[0] + 1) * M_PI / 4; float l = (float)(std::cos(th) * std::sqrt(2.0)), r = (float)(std::sin(th) * std::sqrt(2.0));
        for (int i = 0; i < n; ++i) { io[2 * i] *= l; io[2 * i + 1] *= r; }
    }
};
struct WidthFx : AudioFx {
    void process(float* io, int n, const double* p) override {
        float w = (float)p[0];
        for (int i = 0; i < n; ++i) { float m = (io[2 * i] + io[2 * i + 1]) * 0.5f, s = (io[2 * i] - io[2 * i + 1]) * 0.5f * w; io[2 * i] = m + s; io[2 * i + 1] = m - s; }
    }
};
struct CompFx : AudioFx {
    double env = 0;
    void process(float* io, int n, const double* p) override {
        double thr = p[0], ratio = std::max(p[1], 1.0), at = std::exp(-1.0 / (SR * p[2] / 1000.0)), rl = std::exp(-1.0 / (SR * p[3] / 1000.0)), mk = dbToLin(p[4]);
        for (int i = 0; i < n; ++i) {
            double x = std::max(std::fabs(io[2 * i]), std::fabs(io[2 * i + 1]));
            env = x > env ? at * env + (1 - at) * x : rl * env + (1 - rl) * x;
            double db = 20 * std::log10(std::max(env, 1e-6)), g = 1.0;
            if (db > thr) g = dbToLin(-(db - thr) * (1.0 - 1.0 / ratio));
            float gf = (float)(g * mk);
            io[2 * i] *= gf; io[2 * i + 1] *= gf;
        }
    }
};
struct LimitFx : AudioFx {
    double g = 1;
    void process(float* io, int n, const double* p) override {
        double ceil = dbToLin(p[0]), rl = std::exp(-1.0 / (SR * p[1] / 1000.0));
        for (int i = 0; i < n; ++i) {
            double x = std::max(std::fabs(io[2 * i]), std::fabs(io[2 * i + 1]));
            double target = x > ceil ? ceil / x : 1.0;
            g = target < g ? target : rl * g + (1 - rl) * target;
            io[2 * i] *= (float)g; io[2 * i + 1] *= (float)g;
        }
    }
};
struct GateFx : AudioFx {
    double env = 0, g = 0;
    void process(float* io, int n, const double* p) override {
        double thr = dbToLin(p[0]), at = std::exp(-1.0 / (SR * p[1] / 1000.0)), rl = std::exp(-1.0 / (SR * p[2] / 1000.0));
        for (int i = 0; i < n; ++i) {
            double x = std::max(std::fabs(io[2 * i]), std::fabs(io[2 * i + 1]));
            env = x > env ? x : rl * env + (1 - rl) * x;
            double target = env > thr ? 1.0 : 0.0;
            g = target > g ? at * g + (1 - at) * target : rl * g + (1 - rl) * target;
            io[2 * i] *= (float)g; io[2 * i + 1] *= (float)g;
        }
    }
};
struct EqFx : AudioFx {
    Biquad b[4]; double last[10] = {1e9, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    void process(float* io, int n, const double* p) override {
        if (std::memcmp(last, p, sizeof(last)) != 0) { eqSetup(b, p, SR); std::memcpy(last, p, sizeof(last)); }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c) { float x = io[2 * i + c]; for (int k = 0; k < 4; ++k) x = b[k].tick(c, x); io[2 * i + c] = x; }
    }
};
struct FilterFx : AudioFx {
    Biquad b; bool hp; double last = -1;
    explicit FilterFx(bool highpass) : hp(highpass) {}
    void process(float* io, int n, const double* p) override {
        if (p[0] != last) { b.set(hp ? Biquad::HighPass : Biquad::LowPass, SR, p[0], 0.707, 0); last = p[0]; }
        for (int i = 0; i < n * 2; ++i) io[i] = b.tick(i & 1, io[i]);
    }
};
struct DelayFx : AudioFx {
    std::vector<float> buf; int w = 0;
    DelayFx() : buf((size_t)(SR * 2.0) * 2, 0.f) {}
    void process(float* io, int n, const double* p) override {
        int len = (int)(buf.size() / 2), d = std::min(std::max((int)(p[0] * SR / 1000.0), 1), len - 1);
        float fb = (float)p[1], mix = (float)p[2];
        for (int i = 0; i < n; ++i) {
            int r = (w - d + len) % len;
            for (int c = 0; c < 2; ++c) {
                float dry = io[2 * i + c], wet = buf[(size_t)r * 2 + c];
                buf[(size_t)w * 2 + c] = dry + wet * fb;
                io[2 * i + c] = dry * (1 - mix * 0.5f) + wet * mix;
            }
            w = (w + 1) % len;
        }
    }
};
struct ReverbFx : AudioFx {                                            // Freeverb
    struct Comb { std::vector<float> b; int i = 0; float f = 0; };
    struct AP { std::vector<float> b; int i = 0; };
    Comb c[2][8]; AP a[2][4];
    ReverbFx() {
        static const int ct[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, at[4] = {556, 441, 341, 225};
        for (int ch = 0; ch < 2; ++ch) {
            for (int k = 0; k < 8; ++k) c[ch][k].b.assign(ct[k] + ch * 23, 0.f);
            for (int k = 0; k < 4; ++k) a[ch][k].b.assign(at[k] + ch * 23, 0.f);
        }
    }
    void process(float* io, int n, const double* p) override {
        float room = (float)(p[0] * 0.28 + 0.7), damp = (float)(p[1] * 0.4), mix = (float)p[2];
        for (int s = 0; s < n; ++s) {
            float in = (io[2 * s] + io[2 * s + 1]) * 0.015f, out[2] = {0, 0};
            for (int ch = 0; ch < 2; ++ch) {
                for (int k = 0; k < 8; ++k) {
                    Comb& cb = c[ch][k];
                    float y = cb.b[cb.i];
                    cb.f = y * (1 - damp) + cb.f * damp;
                    cb.b[cb.i] = in + cb.f * room;
                    if (++cb.i >= (int)cb.b.size()) cb.i = 0;
                    out[ch] += y;
                }
                for (int k = 0; k < 4; ++k) {
                    AP& ap = a[ch][k];
                    float bo = ap.b[ap.i], y = -out[ch] + bo;
                    ap.b[ap.i] = out[ch] + bo * 0.5f;
                    if (++ap.i >= (int)ap.b.size()) ap.i = 0;
                    out[ch] = y;
                }
            }
            io[2 * s] = io[2 * s] * (1 - mix) + out[0] * mix * 3.0f;
            io[2 * s + 1] = io[2 * s + 1] * (1 - mix) + out[1] * mix * 3.0f;
        }
    }
};
struct ChorusFx : AudioFx {
    std::vector<float> buf; int w = 0; double ph = 0;
    ChorusFx() : buf((size_t)(SR * 0.06) * 2, 0.f) {}
    void process(float* io, int n, const double* p) override {
        int len = (int)(buf.size() / 2); float mix = (float)p[2];
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) buf[(size_t)w * 2 + c] = io[2 * i + c];
            for (int c = 0; c < 2; ++c) {
                double lfo = std::sin(ph + c * M_PI / 2), d = (p[1] / 1000.0 * SR) * (0.5 + 0.5 * lfo) + 8.0;
                double rp = w - d; while (rp < 0) rp += len;
                int i0 = (int)rp % len, i1 = (i0 + 1) % len; float fr = (float)(rp - std::floor(rp));
                float wet = buf[(size_t)i0 * 2 + c] * (1 - fr) + buf[(size_t)i1 * 2 + c] * fr;
                io[2 * i + c] = io[2 * i + c] * (1 - mix * 0.5f) + wet * mix;
            }
            ph += 2 * M_PI * p[0] / SR; if (ph > 2 * M_PI) ph -= 2 * M_PI;
            w = (w + 1) % len;
        }
    }
};
struct DistFx : AudioFx {
    void process(float* io, int n, const double* p) override {
        float dr = (float)p[0], mix = (float)p[1], norm = 1.0f / std::tanh(dr);
        for (int i = 0; i < n * 2; ++i) io[i] = io[i] * (1 - mix) + std::tanh(io[i] * dr) * norm * mix;
    }
};

struct BassTrebleFx : AudioFx {
    Biquad lo, hi; double last[2] = {1e9, 0};
    void process(float* io, int n, const double* p) override {
        if (last[0] != p[0] || last[1] != p[1]) { lo.set(Biquad::LowShelf, SR, 150, 0.7, p[0]); hi.set(Biquad::HighShelf, SR, 5000, 0.7, p[1]); last[0] = p[0]; last[1] = p[1]; }
        for (int i = 0; i < n * 2; ++i) io[i] = hi.tick(i & 1, lo.tick(i & 1, io[i]));
    }
};
struct FlangeFx : AudioFx {
    std::vector<float> buf; int w = 0; double ph = 0;
    FlangeFx() : buf((size_t)(SR * 0.02) * 2, 0.f) {}
    void process(float* io, int n, const double* p) override {
        int len = (int)(buf.size() / 2); float fb = (float)p[2], mix = (float)p[3];
        for (int i = 0; i < n; ++i) {
            double d = (p[1] / 1000.0 * SR) * (0.5 + 0.5 * std::sin(ph)) + 2.0, rp = w - d;
            while (rp < 0) rp += len;
            int i0 = (int)rp % len, i1 = (i0 + 1) % len; float fr = (float)(rp - std::floor(rp));
            for (int c = 0; c < 2; ++c) {
                float wet = buf[(size_t)i0 * 2 + c] * (1 - fr) + buf[(size_t)i1 * 2 + c] * fr, dry = io[2 * i + c];
                buf[(size_t)w * 2 + c] = dry + wet * fb;
                io[2 * i + c] = dry * (1 - mix * 0.5f) + wet * mix;
            }
            ph += 2 * M_PI * p[0] / SR; if (ph > 2 * M_PI) ph -= 2 * M_PI;
            w = (w + 1) % len;
        }
    }
};
struct ModulatorFx : AudioFx {
    double ph = 0;
    void process(float* io, int n, const double* p) override {
        for (int i = 0; i < n; ++i) {
            double s = std::sin(ph);
            float g = (p[2] > 0.5) ? (float)s : (float)(1.0 - p[1] * (0.5 + 0.5 * s));     // кольцевая модуляция или тремоло
            io[2 * i] *= g; io[2 * i + 1] *= g;
            ph += 2 * M_PI * p[0] / SR; if (ph > 2 * M_PI) ph -= 2 * M_PI;
        }
    }
};
struct StereoMixerFx : AudioFx {
    void process(float* io, int n, const double* p) override {
        float pl = (float)((p[2] + 1) / 2), pr = (float)((p[3] + 1) / 2), ll = (float)p[0], rl = (float)p[1];
        for (int i = 0; i < n; ++i) {
            float l = io[2 * i] * ll, r = io[2 * i + 1] * rl;
            io[2 * i] = l * (1 - pl) + r * (1 - pr);
            io[2 * i + 1] = l * pl + r * pr;
        }
    }
};

std::unique_ptr<AudioFx> makeAudioFx(const QString& n) {
    if (n == "Bass & Treble") return std::unique_ptr<AudioFx>(new BassTrebleFx);
    if (n == "Flange") return std::unique_ptr<AudioFx>(new FlangeFx);
    if (n == "Modulator") return std::unique_ptr<AudioFx>(new ModulatorFx);
    if (n == "Stereo Mixer") return std::unique_ptr<AudioFx>(new StereoMixerFx);
    if (n == "Gain") return std::unique_ptr<AudioFx>(new GainFx);
    if (n == "Pan / Balance") return std::unique_ptr<AudioFx>(new PanFx);
    if (n == "Stereo Width") return std::unique_ptr<AudioFx>(new WidthFx);
    if (n == "Compressor") return std::unique_ptr<AudioFx>(new CompFx);
    if (n == "Limiter") return std::unique_ptr<AudioFx>(new LimitFx);
    if (n == "Noise Gate") return std::unique_ptr<AudioFx>(new GateFx);
    if (n == "EQ 4-band") return std::unique_ptr<AudioFx>(new EqFx);
    if (n == "High-pass Filter") return std::unique_ptr<AudioFx>(new FilterFx(true));
    if (n == "Low-pass Filter") return std::unique_ptr<AudioFx>(new FilterFx(false));
    if (n == "Delay / Echo") return std::unique_ptr<AudioFx>(new DelayFx);
    if (n == "Reverb") return std::unique_ptr<AudioFx>(new ReverbFx);
    if (n == "Chorus") return std::unique_ptr<AudioFx>(new ChorusFx);
    if (n == "Distortion") return std::unique_ptr<AudioFx>(new DistFx);
    return nullptr;
}

void AudioChain::process(float* io, int frames, const std::vector<EffectInst>& fx, double localT) {
    QStringList want;
    for (const EffectInst& e : fx) want << e.name;
    if (want != names) {
        names = want; chain.clear();
        for (const QString& n : names) chain.push_back(makeAudioFx(n));
    }
    for (size_t i = 0; i < fx.size() && i < chain.size(); ++i) {
        const EffectInst& e = fx[i];
        const AudioFxDef* d = findAudioFx(e.name);
        if (!d || !chain[i] || e.params.value("_bypass", 0.0) > 0.5) continue;
        double p[16] = {0};
        for (int k = 0; k < d->params.size() && k < 16; ++k) p[k] = e.value(d->params[k].name, localT, d->params[k].def);
        chain[i]->process(io, frames, p);
    }
}
