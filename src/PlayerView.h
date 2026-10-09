#pragma once
#include "Project.h"
#include "Compositor.h"
#include "AudioEngine.h"
#include <QOpenGLWidget>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>

// Окно предпросмотра: GPU-рендер, плейбек по настенным часам (без дрейфа), transform мышью, запись движения.
class PlayerView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit PlayerView(Project* p, QWidget* parent = nullptr);
    ~PlayerView() override;
    void setProject(Project* p);
    double time() const { return t; }
    bool isPlaying() const { return playing; }
    void setSelected(const ClipPtr& c) { sel = c; update(); }
    Compositor& compositor() { return comp; }
    AudioEngine& audioEngine() { return audio; }
    void setUpdatesPaused(bool b) { paused = b; }
    void setFreeRun(bool b) { freeRun = b; }                 // играть дальше конца проекта (запись голоса)
    bool countingDown() const { return countdown > 0; }
    void setScopesEnabled(bool on) { scopes = on; update(); }
    double previewFps() const { return curFps; }
    QString glInfo() const { return glRenderer; }

public slots:
    void play();
    void pause();
    void toggle();
    void seek(double sec);
    void stepFrames(int n);
    void setRecord(bool r) { rec = r; update(); }
    void setPreviewFx(const QString& name) { previewFx = name; update(); }
    void startCountdown(int seconds);                        // 3-2-1 на видео, потом countdownFinished()
    void cancelCountdown();

signals:
    void timeChanged(double t);
    void playStateChanged(bool playing);
    void transformEdited();
    void clipPicked(ClipPtr c);
    void countdownFinished();
    void aboutToEdit();
    void effectDropped(ClipPtr c, QString spec);
    void textDoubleClicked(ClipPtr c);
    void scopesFrame(QImage frame);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void tick();
    ClipPtr pick(const QPointF& pos);
    void setProp(const ClipPtr& c, const QString& n, double v, double local);

    Project* project;
    Compositor comp;
    AudioEngine audio;
    QTimer timer;
    QElapsedTimer clock;
    double t = 0, t0 = 0;
    bool playing = false, rec = false, paused = false, glReady = false, freeRun = false;
    int countdown = 0;
    QTimer cdTimer;
    bool scopes = false;
    int scopeCtr = 0, frames = 0;
    double curFps = 0;
    QElapsedTimer fpsClock;
    QString glRenderer;
    void grabScopes();
    QString previewFx;
    ClipPtr sel;
    QRectF disp;                       // область кадра в координатах виджета
    bool dragging = false;
    QPointF dragStart;
    double dragX = 0, dragY = 0;
};
