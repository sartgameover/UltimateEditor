#pragma once
#include "Project.h"
#include <QWidget>
#include <QMap>
#include <QVector>

// Таймлайн: дорожки (добавляются по ПКМ), привязка, маркеры, линки A/V, ножницы-направляющая, trim,
// перемещение клипов между дорожками, шарики затухания и анимаций, плашки эффектов с ✕,
// визуализатор звука, панель записи голоса у аудиодорожки, контекстное меню.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(Project* p, QWidget* parent = nullptr);
    void setProject(Project* p);
    double playheadTime() const { return playhead; }
    bool bladeMode() const { return blade; }
    const QList<ClipPtr>& selection() const { return selected; }
    void selectClip(const ClipPtr& c);
    void refreshTracks();
    void applySettings();                         // высота дорожек, привязка, волны, тема
    // запись голоса
    void armTrack(const QString& t) { armed = t; update(); }
    QString armedTrack() const { return armed; }
    void setRecording(bool r) { recording = r; if (!r) recLevel = 0; update(); }
    void setRecordLevel(float l) { recLevel = qMax(l, recLevel * 0.8f); update(); }
    double inputGain() const { return inGain; }

public slots:
    void setPlayhead(double t);
    void setBlade(bool on);
    void setSnap(bool on) { snap = on; }
    void splitAtPlayhead();
    void addMarker();
    void deleteSelected(bool ripple);
    void jumpCut(int dir);
    void selectAll();
    void zoomBy(double factor);
    void zoomFit();

signals:
    void playheadDragged(double t);
    void selectionChanged(ClipPtr c);
    void filesDropped(QStringList paths, double t, QString track);
    void changed();
    void bladeToggled(bool on);
    void aboutToChange();                         // перед правкой проекта (для Undo)
    void tracksChanged();
    void addTextRequested(double t);
    void textEditRequested(ClipPtr c);
    void recordVoiceRequested(QString track);
    void recordButtonClicked(QString track);
    void inputGainChanged(double g);
    void copyRequested();
    void cutRequested();
    void pasteRequested(double t);
    void duplicateRequested();
    void effectDropped(ClipPtr c, QString spec);   // "fx:Glitch" / "afx:Reverb" / "anim:Pop" / "preset:Name"
    void recordScreenRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dropEvent(QDropEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;

private:
    int dropHighlight = -1;
    struct Orig { double start, in, dur; QString track; };
    struct Chip { QRectF close; int clipId; int kind; int index; };   // kind: 0 эффект, 1 вход, 2 выход, 3 цикл
    enum Mode { None, Scrub, Move, TrimL, TrimR, FadeIn, FadeOut, AnimIn, AnimOut, GainDrag };

    double t2x(double t) const;
    double x2t(double x) const;
    int trackY(const QString& n) const;
    QString y2track(double y) const;
    QRectF clipRect(const ClipPtr& c) const;
    ClipPtr clipAt(const QPointF& pos) const;
    ClipPtr clipById(int id) const;
    double snapTime(double t, const QList<double>& edgeOffsets, const QList<ClipPtr>& exclude) const;
    void scrubTo(double x);
    QPointF handlePos(const ClipPtr& c, int which) const;           // 0 fadeIn 1 fadeOut 2 animIn 3 animOut
    bool handleVisible(const ClipPtr& c, int which) const;
    QRectF recButtonRect(const QString& track) const;
    QRectF gainRect(const QString& track) const;
    void beginEdit();

    Project* project;
    double pps = 80, scroll = 0, playhead = 0, mouseX = -1;
    bool blade = false, snap = true, undoPushed = false, recording = false;
    QList<ClipPtr> selected;
    Mode mode = None;
    double dragT0 = 0;
    ClipPtr lead;
    QMap<int, Orig> orig;
    mutable QVector<Chip> chips;
    QString armed;
    double inGain = 1.0;
    float recLevel = 0;
};
