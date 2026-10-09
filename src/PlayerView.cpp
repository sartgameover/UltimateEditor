#include <QDragMoveEvent>
#include "PlayerView.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include "AppSettings.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <cmath>
#include <algorithm>

PlayerView::PlayerView(Project* p, QWidget* parent) : QOpenGLWidget(parent), project(p), audio(p) {
    setMinimumSize(480, 270);
    setMouseTracking(true);
    setAcceptDrops(true);
    comp.setProject(p);
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(4);
    connect(&timer, &QTimer::timeout, this, [this]() { tick(); });
    cdTimer.setInterval(1000);
    connect(&cdTimer, &QTimer::timeout, this, [this]() {
        if (--countdown <= 0) { countdown = 0; cdTimer.stop(); update(); emit countdownFinished(); }
        else update();
    });
}

void PlayerView::startCountdown(int seconds) { countdown = seconds; cdTimer.start(); update(); }
void PlayerView::cancelCountdown() { countdown = 0; cdTimer.stop(); update(); }

PlayerView::~PlayerView() {
    if (glReady) {
        makeCurrent();
        comp.shutdown();
        doneCurrent();
    }
}

void PlayerView::setProject(Project* p) {
    pause();
    project = p;
    if (glReady) {
        makeCurrent();
        comp.resetSources();
        doneCurrent();
    }
    comp.setProject(p);
    audio.setProject(p);
    sel.reset();
    t = 0;
    update();
}

void PlayerView::initializeGL() {
    comp.init();
    glReady = true;
    const GLubyte* r = QOpenGLContext::currentContext()->functions()->glGetString(GL_RENDERER);
    if (r) glRenderer = QString::fromUtf8(reinterpret_cast<const char*>(r));
    fpsClock.start();
}

void PlayerView::grabScopes() {
    std::vector<uint8_t> buf;
    if (!comp.readPixels(buf)) return;
    const int W = project->W, H = project->H, sw = 192, sh = 108;
    QImage img(sw, sh, QImage::Format_RGB888);
    for (int y = 0; y < sh; ++y) {
        const int sy = H - 1 - (y * H / sh);
        for (int x = 0; x < sw; ++x) {
            const uint8_t* px = &buf[((size_t)sy * W + (size_t)(x * W / sw)) * 4];
            uchar* d = img.scanLine(y) + x * 3;
            d[0] = px[0]; d[1] = px[1]; d[2] = px[2];
        }
    }
    emit scopesFrame(img);
}

void PlayerView::mouseDoubleClickEvent(QMouseEvent* e) {
    ClipPtr c = pick(e->position());
    if (c && c->kind == "text") emit textDoubleClicked(c);
}

void PlayerView::paintGL() {
    comp.defaultFbo = defaultFramebufferObject();
    const double dpr = devicePixelRatioF();
    const int pw = (int)(width() * dpr), ph = (int)(height() * dpr);
    GLuint tex = comp.render(t, previewFx, true);
    QRect r = comp.presentLetterboxed(tex, pw, ph);
    disp = QRectF(r.x() / dpr, (ph - r.y() - r.height()) / dpr, r.width() / dpr, r.height() / dpr);
    if (scopes && (++scopeCtr % 4 == 0)) grabScopes();
    if (++frames >= 30 && fpsClock.isValid()) { curFps = frames * 1000.0 / qMax<qint64>(fpsClock.restart(), 1); frames = 0; }

    // оверлей: рамка выбранного объекта
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (sel && sel->kind != "audio" && sel->start <= t && t < sel->end()) {
        int cw = 0, ch = 0;
        if (comp.contentSize(sel->id, cw, ch)) {
            double local = t - sel->start;
            double s0 = Compositor::fitScale(*sel, cw, ch, project->W, project->H);
            double k = disp.width() / project->W;
            AnimMod am = animationMod(*sel, local);
            double sc = sel->prop("scale", local) * am.scale;
            double bw = cw * s0 * sc * k, bh = ch * s0 * sc * k;
            double cx = disp.center().x() + (sel->prop("x", local) + am.dx) * disp.width();
            double cy = disp.center().y() + (sel->prop("y", local) + am.dy) * disp.height();
            p.save();
            p.setPen(QPen(QColor("#ffd54a"), 2, Qt::DashLine));
            p.translate(cx, cy);
            p.rotate(sel->prop("rot", local) + am.rot);
            p.drawRect(QRectF(-bw / 2, -bh / 2, bw, bh));
            p.restore();
        }
    }
    if (AppSettings::showGrid()) {                              // сетка «правило третей»
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        for (int i = 1; i < 3; ++i) {
            p.drawLine(QPointF(disp.left() + disp.width() * i / 3, disp.top()), QPointF(disp.left() + disp.width() * i / 3, disp.bottom()));
            p.drawLine(QPointF(disp.left(), disp.top() + disp.height() * i / 3), QPointF(disp.right(), disp.top() + disp.height() * i / 3));
        }
    }
    if (AppSettings::showSafeZones()) {                         // безопасные зоны 90% и 80%
        p.setPen(QPen(QColor(255, 213, 74, 140), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        for (double k : {0.9, 0.8}) p.drawRect(QRectF(disp.center().x() - disp.width() * k / 2, disp.center().y() - disp.height() * k / 2, disp.width() * k, disp.height() * k));
    }
    p.setPen(QColor("#ff4d4f"));
    if (rec) p.drawText(14, 22, "● REC motion");
    if (!previewFx.isEmpty()) { p.setPen(QColor("#4dabf7")); p.drawText(14, height() - 12, "Preview: " + previewFx); }
    if (countdown > 0) {                                      // 3 … 2 … 1 перед записью голоса
        p.fillRect(rect(), QColor(0, 0, 0, 130));
        QFont f = p.font(); f.setPixelSize(qMax(height() / 3, 40)); f.setBold(true); p.setFont(f);
        p.setPen(QColor("#ff4d4f"));
        p.drawText(rect(), Qt::AlignCenter, QString::number(countdown));
        f.setPixelSize(18); p.setFont(f); p.setPen(Qt::white);
        p.drawText(QRect(0, height() - 40, width(), 30), Qt::AlignCenter, "Запись голоса начнётся…");
    }
}

// ---------- плейбек ----------
void PlayerView::play() {
    if (!freeRun && project->duration() <= 0) return;
    if (!freeRun && t >= project->duration() - 1e-3) t = 0;
    playing = true; t0 = t; clock.start();
    timer.start();
    emit playStateChanged(true);
}

void PlayerView::pause() {
    bool was = playing;
    playing = false; timer.stop();
    audio.stop();
    if (was) emit playStateChanged(false);
}

void PlayerView::toggle() { if (playing) pause(); else play(); }

void PlayerView::tick() {
    if (!playing) return;
    t = t0 + clock.nsecsElapsed() / 1e9;           // по настенным часам: пропуск кадров вместо замедления
    if (!freeRun && t >= project->duration()) {
        if (AppSettings::loopPlayback() && project->duration() > 0.1) { t = 0; t0 = 0; clock.restart(); }       // повтор
        else { t = project->duration(); pause(); }
    }
    audio.sync(t, playing);
    emit timeChanged(t);
    if (!paused) update();
}

void PlayerView::seek(double sec) {
    t = qMax(sec, 0.0);
    if (playing) { t0 = t; clock.restart(); }
    emit timeChanged(t);
    if (!paused) update();
}

void PlayerView::stepFrames(int n) {
    pause();
    seek(t + n / (double)project->fps);
}

// ---------- свободная трансформация ----------
ClipPtr PlayerView::pick(const QPointF& pos) {
    ClipPtr best;
    const QStringList vtracks = project->videoTracksBottomUp();
    for (const QString& trk : vtracks) {
        for (const ClipPtr& c : project->clips) {
            if (c->track != trk || c->kind == "audio" || !(c->start <= t && t < c->end())) continue;
            int cw = 0, ch = 0;
            bool hit = true;
            if (comp.contentSize(c->id, cw, ch)) {
                double local = t - c->start, s0 = Compositor::fitScale(*c, cw, ch, project->W, project->H);
                double bw = cw * s0 * c->prop("scale", local) * disp.width() / project->W;
                double bh = ch * s0 * c->prop("scale", local) * disp.width() / project->W;
                QPointF ctr(disp.center().x() + c->prop("x", local) * disp.width(), disp.center().y() + c->prop("y", local) * disp.height());
                hit = QRectF(ctr.x() - bw / 2, ctr.y() - bh / 2, bw, bh).contains(pos);
            }
            if (hit) best = c;                      // выше по дорожкам — перекрывает
        }
    }
    if (!best) {
        for (const ClipPtr& c : project->clips)
            if (c->kind != "audio" && c->start <= t && t < c->end()) best = c;
    }
    return best;
}

void PlayerView::setProp(const ClipPtr& c, const QString& n, double v, double local) {
    if (rec || (c->keys.contains(n) && !c->keys[n].isEmpty())) setKey(c->keys[n], local, v, 0);
    else c->props[n] = v;
}

void PlayerView::mousePressEvent(QMouseEvent* e) {
    sel = pick(e->position());
    if (sel) {
        emit aboutToEdit();
        double local = t - sel->start;
        dragging = true; dragStart = e->position();
        dragX = sel->prop("x", local); dragY = sel->prop("y", local);
        emit clipPicked(sel);
    }
    update();
}

void PlayerView::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging || !sel || disp.width() <= 0) return;
    double local = t - sel->start;
    setProp(sel, "x", dragX + (e->position().x() - dragStart.x()) / disp.width(), local);
    setProp(sel, "y", dragY + (e->position().y() - dragStart.y()) / disp.height(), local);
    emit transformEdited();
    update();
}

void PlayerView::mouseReleaseEvent(QMouseEvent*) { dragging = false; }

void PlayerView::wheelEvent(QWheelEvent* e) {
    if (!sel) return;
    emit aboutToEdit();
    double local = t - sel->start, dy = e->angleDelta().y();
    if (e->modifiers() & Qt::ShiftModifier) setProp(sel, "rot", sel->prop("rot", local) + dy / 30.0, local);
    else setProp(sel, "scale", qMax(sel->prop("scale", local) * (dy > 0 ? 1.05 : 0.95), 0.02), local);
    emit transformEdited();
    update();
}

void PlayerView::dragEnterEvent(QDragEnterEvent* e) { if (e->mimeData()->hasFormat("application/x-uve-effect")) e->acceptProposedAction(); }
void PlayerView::dragMoveEvent(QDragMoveEvent* e) { e->acceptProposedAction(); }
void PlayerView::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasFormat("application/x-uve-effect")) return;
    ClipPtr c = pick(e->position());                         // эффект падает на тот объект, что под курсором в кадре
    if (c) emit effectDropped(c, QString::fromUtf8(e->mimeData()->data("application/x-uve-effect")));
    e->acceptProposedAction();
}
