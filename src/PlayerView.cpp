#include "PlayerView.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
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
}

void PlayerView::paintGL() {
    comp.defaultFbo = defaultFramebufferObject();
    const double dpr = devicePixelRatioF();
    const int pw = (int)(width() * dpr), ph = (int)(height() * dpr);
    GLuint tex = comp.render(t, previewFx, true);
    QRect r = comp.presentLetterboxed(tex, pw, ph);
    disp = QRectF(r.x() / dpr, (ph - r.y() - r.height()) / dpr, r.width() / dpr, r.height() / dpr);

    // оверлей: рамка выбранного объекта
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (sel && sel->kind != "audio" && sel->start <= t && t < sel->end()) {
        int cw = 0, ch = 0;
        if (comp.contentSize(sel->id, cw, ch)) {
            double local = t - sel->start;
            double s0 = std::min(project->W / (double)cw, project->H / (double)ch);
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
    if (!freeRun && t >= project->duration()) { t = project->duration(); pause(); }
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
                double local = t - c->start, s0 = std::min(project->W / (double)cw, project->H / (double)ch);
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
