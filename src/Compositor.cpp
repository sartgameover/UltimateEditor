#include "Compositor.h"
#include "Effects.h"
#include "Peaks.h"
#include <QImage>
#include <QPainter>
#include <QMatrix4x4>
#include <QDebug>
#include <algorithm>
#include <cmath>

static const char* kCompFrag = R"GLSL(#version 330 core
in vec2 vUV;
out vec4 oColor;
uniform sampler2D uTex;
uniform float uOpacity;
void main() { vec4 c = texture(uTex, vUV); oColor = vec4(c.rgb, c.a * uOpacity); }
)GLSL";

void Compositor::init() {
    initializeOpenGLFunctions();
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    comp = new QOpenGLShaderProgram;
    comp->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSource);
    comp->addShaderFromSourceCode(QOpenGLShader::Fragment, kCompFrag);
    if (!comp->link()) qWarning() << "comp shader:" << comp->log();
}

void Compositor::freeLayer(Layer& L) {
    if (L.tex[0]) glDeleteTextures(2, L.tex);
    L.tex[0] = L.tex[1] = 0;
}

void Compositor::resetSources() {
    for (auto& kv : layers) freeLayer(kv.second);
    for (auto& kv : statics) freeLayer(kv.second);
    layers.clear(); statics.clear(); decoders.clear(); failed.clear(); sizes.clear();
}

void Compositor::shutdown() {
    resetSources();
    for (auto* p : progs) delete p;
    progs.clear();
    for (auto* f : pools) delete f;
    pools.clear();
    delete canvas; canvas = nullptr;
    delete comp; comp = nullptr;
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
}

void Compositor::setupTexParams(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Compositor::ensureCanvas() {
    if (canvas && canvas->width() == project->W && canvas->height() == project->H) return;
    delete canvas;
    canvas = new QOpenGLFramebufferObject(project->W, project->H);
    setupTexParams(canvas->texture());
}

QOpenGLFramebufferObject* Compositor::pool(int w, int h, int slot) {
    quint64 key = ((quint64)w << 32) | ((quint64)h << 1) | (quint64)slot;
    auto it = pools.find(key);
    if (it != pools.end()) return it.value();
    QOpenGLFramebufferObject* f = new QOpenGLFramebufferObject(w, h);
    setupTexParams(f->texture());
    pools.insert(key, f);
    return f;
}

QOpenGLShaderProgram* Compositor::programFor(const EffectDef& d) {
    auto it = progs.find(d.name);
    if (it != progs.end()) return it.value();
    QOpenGLShaderProgram* p = new QOpenGLShaderProgram;
    p->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSource);
    p->addShaderFromSourceCode(QOpenGLShader::Fragment, effectFragmentSource(d));
    if (!p->link()) qWarning() << "effect shader" << d.name << p->log();
    progs.insert(d.name, p);
    return p;
}

void Compositor::upload(Layer& L, const uint8_t* data, int w, int h, bool isNew) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!L.tex[0] || L.w != w || L.h != h) {
        if (!L.tex[0]) glGenTextures(2, L.tex);
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, L.tex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            setupTexParams(L.tex[i]);
        }
        L.w = w; L.h = h; L.cur = 0; L.sample.clear(); L.motion = 0;
    } else if (isNew) {
        int nxt = 1 - L.cur;
        glBindTexture(GL_TEXTURE_2D, L.tex[nxt]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data);
        L.cur = nxt;
    } else {
        return;
    }
    // движение: сравнение редкой выборки пикселей с прошлым кадром
    size_t n = (size_t)w * h, step = n / 2048 + 1;
    std::vector<uint8_t> s; s.reserve(n / step + 1);
    for (size_t i = 0; i < n; i += step) s.push_back(data[i * 4 + 1]);
    if (L.sample.size() == s.size()) {
        double sum = 0;
        for (size_t i = 0; i < s.size(); ++i) sum += std::abs((int)s[i] - (int)L.sample[i]);
        L.motion = std::min(sum / s.size() / 12.0, 1.0);
    }
    L.sample.swap(s);
}

static QImage renderText(const QString& text, const QString& color, double size, int W, int H) {
    QImage img(W, H, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f("Sans Serif"); f.setPixelSize(qMax((int)(H / 12 * size), 8)); f.setBold(true); p.setFont(f);
    QRectF r(W * 0.05, H * 0.6, W * 0.9, H * 0.35);
    int fl = Qt::AlignCenter | Qt::TextWordWrap;
    p.setPen(Qt::black);
    for (int i = 0; i < 4; ++i) {
        static const int dx[4] = {-2, 2, 0, 0}, dy[4] = {0, 0, -2, 2};
        p.drawText(r.translated(dx[i], dy[i]), fl, text);
    }
    p.setPen(QColor(color));
    p.drawText(r, fl, text);
    p.end();
    return img.convertToFormat(QImage::Format_RGBA8888);
}

bool Compositor::sourceFor(const ClipPtr& c, double local, GLuint& tex, GLuint& prev, int& w, int& h, double& motion) {
    motion = 0;
    if (c->kind == "text") {
        QString key = QString("t|%1|%2|%3|%4x%5").arg(c->text, c->textColor).arg(c->textSize).arg(project->W).arg(project->H);
        if (!statics.count(key)) {                                 // чистим старые версии текста
            int n = 0;
            for (auto& kv : statics) if (kv.first.startsWith("t|")) ++n;
            if (n > 24) {
                for (auto it = statics.begin(); it != statics.end();) {
                    if (it->first.startsWith("t|")) { freeLayer(it->second); it = statics.erase(it); } else ++it;
                }
            }
        }
        Layer& L = statics[key];
        if (!L.tex[0]) {
            QImage img = renderText(c->text.isEmpty() ? QString("Text") : c->text, c->textColor, c->textSize, project->W, project->H);
            upload(L, img.constBits(), img.width(), img.height(), true);
        }
        tex = prev = L.tex[0]; w = L.w; h = L.h;
        return true;
    }
    auto ai = project->assets.constFind(c->assetId);
    if (ai == project->assets.constEnd()) return false;
    const Asset& a = ai.value();
    if (c->kind == "image") {
        Layer& L = statics["i|" + a.path];
        if (!L.tex[0]) {
            QImage img(a.path);
            if (img.isNull()) return false;
            if (img.width() > 4096) img = img.scaledToWidth(4096, Qt::SmoothTransformation);
            img = img.convertToFormat(QImage::Format_RGBA8888);
            upload(L, img.constBits(), img.width(), img.height(), true);
        }
        tex = prev = L.tex[0]; w = L.w; h = L.h;
        return true;
    }
    // видео
    if (failed.count(c->id)) return false;
    auto& dec = decoders[c->id];
    if (!dec) {
        dec.reset(new VideoDecoder);
        QString path = (useProxy && !a.proxy.isEmpty()) ? a.proxy : a.path;
        if (!dec->open(path, qMax(project->W * 2, 1920))) { decoders.erase(c->id); failed[c->id] = true; return false; }
    }
    const uint8_t* data = nullptr; int dw = 0, dh = 0; bool isNew = false;
    if (!dec->frameAt(c->in + local, data, dw, dh, isNew)) return false;
    Layer& L = layers[c->id];
    upload(L, data, dw, dh, isNew || !L.tex[0]);
    tex = L.tex[L.cur]; prev = L.tex[1 - L.cur]; w = L.w; h = L.h; motion = L.motion;
    return true;
}

GLuint Compositor::runEffects(const std::vector<EffectInst>& fxs, double local, GLuint src, GLuint prev, int w, int h,
                              float audio, float motion, double t) {
    GLuint in = src;
    int slot = 0;
    glDisable(GL_BLEND);
    for (const EffectInst& e : fxs) {
        const EffectDef* d = findEffect(e.name);
        if (!d) continue;
        QOpenGLShaderProgram* pr = programFor(*d);
        if (!pr->isLinked()) continue;
        QOpenGLFramebufferObject* f = pool(w, h, slot);
        f->bind();
        glViewport(0, 0, w, h);
        pr->bind();
        QMatrix4x4 id;
        pr->setUniformValue("uMVP", id);
        auto loc = [&](const char* n) { return pr->uniformLocation(n); };
        if (loc("uTex") >= 0) glUniform1i(loc("uTex"), 0);
        if (loc("uPrev") >= 0) glUniform1i(loc("uPrev"), 1);
        if (loc("uRes") >= 0) glUniform2f(loc("uRes"), (float)w, (float)h);
        if (loc("uTime") >= 0) glUniform1f(loc("uTime"), (float)t);
        if (loc("uAudio") >= 0) glUniform1f(loc("uAudio"), audio);
        if (loc("uMotion") >= 0) glUniform1f(loc("uMotion"), motion);
        for (int i = 0; i < d->params.size(); ++i) {
            QByteArray nm = "p" + QByteArray::number(i);
            int l = loc(nm.constData());
            if (l >= 0) glUniform1f(l, (float)e.value(d->params[i].name, local, d->params[i].def));
        }
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, in);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, prev);
        drawQuad();
        in = f->texture();
        slot ^= 1;
    }
    glActiveTexture(GL_TEXTURE0);
    return in;
}

void Compositor::drawLayer(const ClipPtr& c, double t, double fade) {
    double local = t - c->start;
    GLuint tex = 0, prev = 0; int sw = 0, sh = 0; double motion = 0;
    double srcLocal = local;                                    // Posterize Time: держим кадр источника на заданном fps
    for (const EffectInst& e : c->effects)
        if (e.name == "Posterize Time") { double f = qMax(e.value("fps", local, 12.0), 1.0); srcLocal = std::floor(local * f) / f; }
    if (!sourceFor(c, srcLocal, tex, prev, sw, sh, motion)) return;
    sizes[c->id] = QSize(sw, sh);
    const int W = project->W, H = project->H;
    double s0 = std::min(W / (double)sw, H / (double)sh);
    int fw = qMax(1, (int)(sw * s0 + 0.5)), fh = qMax(1, (int)(sh * s0 + 0.5));
    if (!c->effects.empty()) {
        float audio = 0.f;
        auto ai = project->assets.constFind(c->assetId);
        if (ai != project->assets.constEnd() && ai.value().hasAudio) {
            Peaks::instance().request(ai.value().path);
            audio = Peaks::instance().level(ai.value().path, c->in + local);
        }
        tex = runEffects(c->effects, local, tex, prev, fw, fh, audio, (float)motion, t);
        canvas->bind();
        glViewport(0, 0, W, H);
        glEnable(GL_BLEND);
    }
    AnimMod am = animationMod(*c, local);                      // анимации входа/выхода/цикла
    double scale = c->prop("scale", local) * am.scale, rot = c->prop("rot", local) + am.rot;
    double cx = W / 2.0 + (c->prop("x", local) + am.dx) * W, cy = H / 2.0 + (c->prop("y", local) + am.dy) * H;
    double opacity = c->prop("opacity", local) * fade * c->fadeFactor(local) * am.opacity;
    QMatrix4x4 m;
    m.ortho(0, W, H, 0, -1, 1);
    m.translate((float)cx, (float)cy);
    m.rotate((float)rot, 0, 0, 1);
    m.scale((float)(fw * scale / 2.0), (float)(fh * scale / 2.0), 1.f);
    comp->bind();
    comp->setUniformValue("uMVP", m);
    comp->setUniformValue("uOpacity", (float)opacity);
    comp->setUniformValue("uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    drawQuad();
}

GLuint Compositor::render(double t, const QString& previewFx, bool useProxy_) {
    if (!project) return 0;
    useProxy = useProxy_;
    ensureCanvas();
    if (pools.size() > 16) { for (auto* f : pools) delete f; pools.clear(); }
    glBindVertexArray(vao);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
    canvas->bind();
    glViewport(0, 0, project->W, project->H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // выгрузка неактивных декодеров, если их много
    if (decoders.size() > 6) {
        for (auto it = decoders.begin(); it != decoders.end();) {
            bool active = false;
            for (const ClipPtr& c : project->clips) if (c->id == it->first && c->start <= t && t < c->end()) active = true;
            if (!active) { auto lt = layers.find(it->first); if (lt != layers.end()) { freeLayer(lt->second); layers.erase(lt); } it = decoders.erase(it); }
            else ++it;
        }
    }

    const QStringList vtracks = project->videoTracksBottomUp();
    for (const QString& trk : vtracks) {
        QList<ClipPtr> list;
        for (const ClipPtr& c : project->clips) if (c->track == trk && c->kind != "audio") list.append(c);
        std::sort(list.begin(), list.end(), [](const ClipPtr& a, const ClipPtr& b) { return a->start < b->start; });
        for (int i = 0; i < list.size(); ++i) {
            const ClipPtr& c = list[i];
            if (!(c->start <= t && t < c->end())) continue;
            double fade = 1.0;
            if (i > 0 && list[i - 1]->end() > c->start)        // перекрытие клипов = Cross Dissolve
                fade = qBound(0.0, (t - c->start) / qMax(list[i - 1]->end() - c->start, 1e-6), 1.0);
            drawLayer(c, t, fade);
        }
    }
    GLuint out = canvas->texture();
    if (!previewFx.isEmpty()) {                                // hover-предпросмотр эффекта на текущем кадре
        const EffectDef* d = findEffect(previewFx);
        if (d) {
            EffectInst e; e.name = d->name;
            for (const ParamDef& p : d->params) e.params[p.name] = p.def;
            std::vector<EffectInst> v(1, e);
            out = runEffects(v, 0, out, out, project->W, project->H, 0.6f, 0.6f, t);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    return out;
}

QRect Compositor::presentLetterboxed(GLuint tex, int vw, int vh) {
    glBindVertexArray(vao);
    glViewport(0, 0, vw, vh);
    glClearColor(0.07f, 0.07f, 0.075f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    double s = std::min(vw / (double)project->W, vh / (double)project->H);
    int rw = (int)(project->W * s), rh = (int)(project->H * s);
    int x = (vw - rw) / 2, y = (vh - rh) / 2;
    glViewport(x, y, rw, rh);
    glDisable(GL_BLEND);
    QMatrix4x4 id;
    comp->bind();
    comp->setUniformValue("uMVP", id);
    comp->setUniformValue("uOpacity", 1.0f);
    comp->setUniformValue("uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    drawQuad();
    return QRect(x, y, rw, rh);
}

bool Compositor::readPixels(std::vector<uint8_t>& out) {
    if (!canvas) return false;
    out.resize((size_t)project->W * project->H * 4);
    canvas->bind();
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, project->W, project->H, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFbo);
    return true;
}

bool Compositor::contentSize(int clipId, int& w, int& h) const {
    auto it = sizes.find(clipId);
    if (it == sizes.end()) return false;
    w = it->second.width(); h = it->second.height();
    return true;
}
