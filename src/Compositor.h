#pragma once
#include "Project.h"
#include "Decoder.h"
#include "Effects.h"
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QHash>
#include <QRect>
#include <map>
#include <memory>
#include <algorithm>

// GPU-компоновка кадра: декодер -> текстура -> шейдерные эффекты (ping-pong FBO) -> трансформация -> слои.
// Один и тот же код — для плеера и для экспорта.
class Compositor : protected QOpenGLFunctions_3_3_Core {
public:
    GLuint defaultFbo = 0;
    // масштаб «вписывания» источника в кадр (текст рисуется 1:1, у него тесные границы)
    static double fitScale(const Clip& c, int cw, int ch, int W, int H) { return c.kind == "text" ? 1.0 : std::min(W / (double)cw, H / (double)ch); }                      // куда вернуть рендер после работы (FBO виджета)

    Compositor() {}
    ~Compositor() {}
    void init();                                // при текущем GL-контексте
    void shutdown();                            // при текущем GL-контексте
    void setProject(Project* p) { project = p; }
    void resetSources();                        // сбросить декодеры/текстуры (смена проекта)
    GLuint render(double t, const QString& previewFx, bool useProxy);
    QRect presentLetterboxed(GLuint tex, int viewW, int viewH);   // рисует в текущий FBO; возвращает rect в px (GL, снизу вверх)
    bool readPixels(std::vector<uint8_t>& out);                   // RGBA, строки снизу вверх
    bool contentSize(int clipId, int& w, int& h) const;           // последний размер источника (для рамки выделения)

private:
    struct Layer { GLuint tex[2] = {0, 0}; int cur = 0, w = 0, h = 0; std::vector<uint8_t> sample; double motion = 0; };

    void ensureCanvas();
    void freeLayer(Layer& L);
    void upload(Layer& L, const uint8_t* data, int w, int h, bool isNew);
    bool sourceFor(const ClipPtr& c, double local, GLuint& tex, GLuint& prev, int& w, int& h, double& motion);
    void drawLayer(const ClipPtr& c, double t, double fade);
    GLuint runEffects(const std::vector<EffectInst>& fx, double local, GLuint src, GLuint prev, int w, int h,
                      float audio, float motion, double t);
    QOpenGLShaderProgram* programFor(const EffectDef& d);
    QOpenGLFramebufferObject* pool(int w, int h, int slot);
    void setupTexParams(GLuint tex);
    void drawQuad() { glDrawArrays(GL_TRIANGLE_STRIP, 0, 4); }

    Project* project = nullptr;
    bool useProxy = true;
    GLuint vao = 0;
    QOpenGLShaderProgram* comp = nullptr;
    QOpenGLFramebufferObject* canvas = nullptr;
    QHash<QString, QOpenGLShaderProgram*> progs;
    QHash<quint64, QOpenGLFramebufferObject*> pools;
    std::map<int, std::unique_ptr<VideoDecoder>> decoders;
    std::map<int, bool> failed;
    std::map<int, Layer> layers;                // по id клипа (видео)
    std::map<QString, Layer> statics;           // картинки и текст
    std::map<int, QSize> sizes;
};
