#include "Timeline.h"
#include "Peaks.h"
#include "Effects.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QMenu>
#include <QInputDialog>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

static const int HEAD_W = 96, RULER_H = 26, TRACK_H = 52;

static QString fmtTime(double t) {
    int m = (int)(t / 60);
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(t - m * 60, 5, 'f', 1, QChar('0'));
}

TimelineWidget::TimelineWidget(Project* p, QWidget* parent) : QWidget(parent), project(p) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    refreshTracks();
    connect(&Peaks::instance(), &Peaks::ready, this, [this](const QString&) { update(); });
}

void TimelineWidget::setProject(Project* p) { project = p; selected.clear(); refreshTracks(); update(); }
void TimelineWidget::refreshTracks() { setMinimumHeight(RULER_H + TRACK_H * project->tracks.size() + 4); updateGeometry(); update(); }

double TimelineWidget::t2x(double t) const { return HEAD_W + (t - scroll) * pps; }
double TimelineWidget::x2t(double x) const { return qMax((x - HEAD_W) / pps + scroll, 0.0); }
int TimelineWidget::trackY(const QString& n) const { return RULER_H + qMax(project->tracks.indexOf(n), 0) * TRACK_H; }
QString TimelineWidget::y2track(double y) const {
    int i = (int)std::floor((y - RULER_H) / TRACK_H);
    return project->tracks[qBound(0, i, project->tracks.size() - 1)];
}
QRectF TimelineWidget::clipRect(const ClipPtr& c) const {
    return QRectF(t2x(c->start), trackY(c->track) + 2, c->dur * pps, TRACK_H - 4);
}
ClipPtr TimelineWidget::clipAt(const QPointF& pos) const {
    for (int i = project->clips.size() - 1; i >= 0; --i)
        if (clipRect(project->clips[i]).contains(pos)) return project->clips[i];
    return ClipPtr();
}
ClipPtr TimelineWidget::clipById(int id) const {
    for (const ClipPtr& c : project->clips) if (c->id == id) return c;
    return ClipPtr();
}

double TimelineWidget::snapTime(double t, const QList<double>& offs, const QList<ClipPtr>& exclude) const {
    if (!snap) return t;
    QList<double> pts; pts << 0.0 << playhead;
    for (const Marker& m : project->markers) pts << m.t;
    for (const ClipPtr& c : project->clips) if (!exclude.contains(c)) pts << c->start << c->end();
    double best = t, th = 8.0 / pps;
    for (double p : pts)
        for (double o : offs)
            if (std::abs(t + o - p) < th) { best = p - o; th = std::abs(t + o - p); }
    return best;
}

void TimelineWidget::selectClip(const ClipPtr& c) {
    selected.clear();
    if (c) selected = project->group(c);
    emit selectionChanged(c);
    update();
}

void TimelineWidget::selectAll() {
    selected = project->clips;
    emit selectionChanged(selected.isEmpty() ? ClipPtr() : selected.first());
    update();
}

void TimelineWidget::setPlayhead(double t) {
    playhead = qMax(t, 0.0);
    double right = scroll + (width() - HEAD_W) / pps;
    if (playhead > right) scroll = playhead - 1;
    if (playhead < scroll) scroll = qMax(playhead - 1, 0.0);
    update();
}

void TimelineWidget::setBlade(bool on) {
    if (blade == on) return;
    blade = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    emit bladeToggled(on);
    update();
}

void TimelineWidget::zoomBy(double f) {
    pps = qBound(5.0, pps * f, 1000.0);
    scroll = qMax(playhead - (width() - HEAD_W) / 2.0 / pps, 0.0);
    update();
}
void TimelineWidget::zoomFit() {
    double d = project->duration();
    if (d > 0.1) pps = qBound(5.0, (width() - HEAD_W - 30) / d, 1000.0);
    scroll = 0;
    update();
}

void TimelineWidget::beginEdit() {
    if (!undoPushed) { emit aboutToChange(); undoPushed = true; }
}

// ---------- геометрия «шариков» ----------
bool TimelineWidget::handleVisible(const ClipPtr& c, int which) const {
    if (clipRect(c).width() < 24) return false;
    if (which == 2) return c->kind != "audio" && !c->animIn.isEmpty();
    if (which == 3) return c->kind != "audio" && !c->animOut.isEmpty();
    return true;
}
QPointF TimelineWidget::handlePos(const ClipPtr& c, int which) const {
    QRectF r = clipRect(c);
    switch (which) {
        case 0: return QPointF(r.left() + qMin(c->fadeIn * pps, r.width()), r.top() + 8);
        case 1: return QPointF(r.right() - qMin(c->fadeOut * pps, r.width()), r.top() + 8);
        case 2: return QPointF(r.left() + qMin(c->animInDur * pps, r.width()), r.bottom() - 8);
        default: return QPointF(r.right() - qMin(c->animOutDur * pps, r.width()), r.bottom() - 8);
    }
}
QRectF TimelineWidget::recButtonRect(const QString& track) const { return QRectF(6, trackY(track) + 6, 22, 22); }
QRectF TimelineWidget::gainRect(const QString& track) const { return QRectF(8, trackY(track) + TRACK_H - 15, HEAD_W - 16, 8); }

// ---------- отрисовка ----------
static QColor levelColor(float v) {
    QColor a("#2ee6a6"), b("#ffd54a"), c("#ff5a5f");
    if (v < 0.6f) { float u = v / 0.6f; return QColor(a.red() + (b.red() - a.red()) * u, a.green() + (b.green() - a.green()) * u, a.blue() + (b.blue() - a.blue()) * u); }
    float u = (v - 0.6f) / 0.4f;
    return QColor(b.red() + (c.red() - b.red()) * u, b.green() + (c.green() - b.green()) * u, b.blue() + (c.blue() - b.blue()) * u);
}

void TimelineWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    chips.clear();
    p.fillRect(rect(), QColor("#1e1f22"));
    for (const QString& n : project->tracks) {
        int y = trackY(n);
        p.fillRect(0, y, width(), TRACK_H, QColor(isVideoTrack(n) ? "#26282c" : "#22262a"));
        p.setPen(QColor("#3a3d42"));
        p.drawLine(0, y + TRACK_H, width(), y + TRACK_H);
    }
    // линейка
    p.fillRect(0, 0, width(), RULER_H, QColor("#2b2d31"));
    static const double steps[] = {0.1, 0.5, 1, 2, 5, 10, 30, 60, 300};
    double step = 300;
    for (double s : steps) if (s * pps >= 60) { step = s; break; }
    p.setPen(QColor("#9aa0a6"));
    p.setFont(QFont("Sans", 8));
    for (double t = std::floor(scroll / step) * step; t2x(t) < width(); t += step) {
        double x = t2x(t);
        if (x < HEAD_W) continue;
        p.drawLine(QPointF(x, RULER_H - 8), QPointF(x, RULER_H));
        p.drawText(QPointF(x + 3, 12), fmtTime(t));
    }
    // клипы
    QFont small("Sans", 8);
    for (const ClipPtr& c : project->clips) {
        if (!project->tracks.contains(c->track)) continue;
        QRectF r = clipRect(c);
        if (r.right() < HEAD_W || r.left() > width()) continue;
        QColor col = c->kind == "video" ? QColor("#3b6ea5") : c->kind == "image" ? QColor("#6a8f3b")
                   : c->kind == "audio" ? QColor("#2f6f62") : QColor("#a0602f");
        bool sel = selected.contains(c);
        p.setBrush(sel ? col.lighter(125) : col);
        p.setPen(QPen(sel ? QColor("#ffd54a") : QColor(0, 0, 0, 140), sel ? 2 : 1));
        p.drawRoundedRect(r, 4, 4);

        // визуализатор звука: у аудиоклипа — крупно, у видео со звуком — тонкой полосой снизу
        auto ai = project->assets.constFind(c->assetId);
        bool hasWave = (c->kind == "audio" || (c->kind == "video" && ai != project->assets.constEnd() && ai.value().hasAudio));
        if (hasWave && ai != project->assets.constEnd()) {
            const QVector<float>* pk = Peaks::instance().get(ai.value().path);
            if (!pk) Peaks::instance().request(ai.value().path);
            else {
                bool big = c->kind == "audio";
                double mid = big ? r.center().y() + 4 : r.bottom() - 9, amp = big ? r.height() / 2 - 8 : 7;
                int x0 = qMax((int)r.left(), HEAD_W), x1 = qMin((int)r.right(), width());
                int win = qMax(1, (int)(Peaks::PER_SEC * 3 / pps));
                QPen bar; bar.setWidthF(2.0); bar.setCapStyle(Qt::RoundCap);
                for (int x = x0; x < x1; x += 3) {
                    double tt = c->in + (x - r.left()) / pps;
                    int i = (int)(tt * Peaks::PER_SEC);
                    float v = 0;
                    for (int k = i; k < i + win && k < pk->size(); ++k) v = qMax(v, pk->at(k));
                    double h = qMax(v * amp, 1.0);
                    QColor lc = levelColor(v);
                    if (!big) lc.setAlpha(150);
                    bar.setColor(lc);
                    p.setPen(bar);
                    p.drawLine(QPointF(x, mid - h), QPointF(x, mid + h));
                }
            }
        }

        // затухание: затемнённые клинья + шарики
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 110));
        if (c->fadeIn > 0.001) {
            QPointF h = handlePos(c, 0);
            QPolygonF poly; poly << QPointF(r.left(), r.top()) << QPointF(h.x(), r.top()) << QPointF(r.left(), r.bottom());
            p.drawPolygon(poly);
        }
        if (c->fadeOut > 0.001) {
            QPointF h = handlePos(c, 1);
            QPolygonF poly; poly << QPointF(r.right(), r.top()) << QPointF(h.x(), r.top()) << QPointF(r.right(), r.bottom());
            p.drawPolygon(poly);
        }

        // название и плашки эффектов/анимаций с ✕
        p.setFont(small);
        QFontMetrics fm(small);
        QString label;
        if (c->kind == "text") label = c->text;
        else if (ai != project->assets.constEnd()) label = ai.value().path.section('/', -1);
        if (c->link) label = "🔗 " + label;
        p.save();
        p.setClipRect(r.adjusted(3, 0, -3, 0));
        p.setPen(Qt::white);
        p.drawText(r.adjusted(5, 3, 0, 0), Qt::AlignTop | Qt::AlignLeft, label);
        double cx = r.left() + 5 + fm.horizontalAdvance(label) + 8;
        auto chip = [&](const QString& text, const QColor& bg, int kind, int index) {
            double w = fm.horizontalAdvance(text) + 24;
            if (cx + w > r.right() - 4) return;
            QRectF cr(cx, r.top() + 3, w, 16);
            p.setBrush(bg); p.setPen(Qt::NoPen);
            p.drawRoundedRect(cr, 8, 8);
            p.setPen(Qt::white);
            p.drawText(cr.adjusted(7, 0, -16, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
            QRectF xr(cr.right() - 15, cr.top(), 15, 16);
            p.drawText(xr, Qt::AlignCenter, "✕");
            Chip ch; ch.close = xr; ch.clipId = c->id; ch.kind = kind; ch.index = index;
            chips.append(ch);
            cx += w + 4;
        };
        for (int i = 0; i < (int)c->effects.size(); ++i) chip("fx " + c->effects[i].name, QColor("#b5701c"), 0, i);
        if (!c->animIn.isEmpty()) chip("▶ " + c->animIn, QColor("#1c8fa0"), 1, 0);
        if (!c->animOut.isEmpty()) chip("◀ " + c->animOut, QColor("#1c8fa0"), 2, 0);
        if (!c->animLoop.isEmpty()) chip("∞ " + c->animLoop, QColor("#1c8fa0"), 3, 0);
        p.restore();

        // шарики: белые — затухание (сверху), голубые — длительность анимаций (снизу)
        if (r.width() >= 24) {
            for (int w = 0; w < 4; ++w) {
                if (!handleVisible(c, w)) continue;
                QPointF h = handlePos(c, w);
                p.setPen(QPen(QColor("#202124"), 1.5));
                p.setBrush(w < 2 ? QColor("#ffffff") : QColor("#4dd0e1"));
                p.drawEllipse(h, 5, 5);
            }
        }
        if (dropHighlight == c->id) {                                  // клип-цель при перетаскивании эффекта
            p.setBrush(QColor(77, 208, 225, 60));
            p.setPen(QPen(QColor("#4dd0e1"), 3));
            p.drawRoundedRect(r, 4, 4);
        }
        // подсветка клипа под курсором-лезвием
        if (blade && mouseX >= 0 && r.left() <= mouseX && mouseX <= r.right()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#ffffff"), 2));
            p.drawRoundedRect(r, 4, 4);
        }
    }
    // маркеры
    for (const Marker& m : project->markers) {
        double x = t2x(m.t);
        p.setBrush(QColor(m.color)); p.setPen(Qt::NoPen);
        QPolygonF tri; tri << QPointF(x - 5, 0) << QPointF(x + 5, 0) << QPointF(x, 10);
        p.drawPolygon(tri);
    }
    // заголовки дорожек; у выбранной для записи — кнопка записи и громкость
    p.setFont(QFont("Sans", 9));
    for (const QString& n : project->tracks) {
        int y = trackY(n);
        p.fillRect(0, y, HEAD_W, TRACK_H, QColor("#2b2d31"));
        p.setPen(QColor("#d0d3d8"));
        if (n == armed) {
            p.drawText(QRectF(32, y + 4, HEAD_W - 34, 26), Qt::AlignVCenter | Qt::AlignLeft, n + " 🎙");
            QRectF b = recButtonRect(n);
            p.setPen(QPen(QColor("#ffffff"), 2));
            p.setBrush(QColor("#e5484d"));
            p.drawEllipse(b);
            if (recording) { p.setBrush(Qt::white); p.setPen(Qt::NoPen); p.drawRect(QRectF(b.center().x() - 5, b.center().y() - 5, 10, 10)); }
            QRectF g = gainRect(n);
            p.setPen(Qt::NoPen); p.setBrush(QColor("#3a3d42")); p.drawRoundedRect(g, 4, 4);
            p.setBrush(QColor("#46a758")); p.drawRoundedRect(QRectF(g.left(), g.top(), g.width() * inGain, g.height()), 4, 4);
            p.setBrush(Qt::white); p.drawEllipse(QPointF(g.left() + g.width() * inGain, g.center().y()), 6, 6);
            if (recording) {                                        // индикатор входного уровня
                p.setBrush(levelColor(recLevel));
                p.drawRect(QRectF(g.left(), g.top() - 7, g.width() * qMin(recLevel, 1.0f), 4));
            }
        } else {
            p.drawText(QRectF(0, y, HEAD_W, TRACK_H), Qt::AlignCenter, n);
        }
    }
    // плейхед (красная линия + «ручка»)
    double x = t2x(playhead);
    p.setPen(QPen(QColor("#ff4d4f"), 2));
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
    p.setBrush(QColor("#ff4d4f"));
    QPolygonF h; h << QPointF(x - 7, 0) << QPointF(x + 7, 0) << QPointF(x + 7, 9) << QPointF(x, 16) << QPointF(x - 7, 9);
    p.drawPolygon(h);
    // направляющая ножниц
    if (blade && mouseX >= HEAD_W) {
        double gx = t2x(snapTime(x2t(mouseX), QList<double>() << 0.0, QList<ClipPtr>()));
        p.setPen(QPen(QColor("#ffffff"), 1, Qt::DashLine));
        p.drawLine(QPointF(gx, RULER_H), QPointF(gx, height()));
        p.setPen(QColor("#ffffff"));
        p.setFont(QFont("Sans", 11));
        p.drawText(QPointF(gx - 7, RULER_H + 14), "✂");
    }
}

// ---------- мышь ----------
void TimelineWidget::scrubTo(double x) {
    setPlayhead(snapTime(x2t(x), QList<double>() << 0.0, QList<ClipPtr>()));
    emit playheadDragged(playhead);
}

void TimelineWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();
    QPointF pos = e->position();
    mouseX = pos.x();
    undoPushed = false;
    if (e->button() == Qt::RightButton) {                         // меню покажет contextMenuEvent
        ClipPtr c = clipAt(pos);
        if (c && !selected.contains(c)) selectClip(c);
        return;
    }
    // панель записи голоса у заголовка дорожки
    if (pos.x() < HEAD_W && pos.y() >= RULER_H) {
        if (!armed.isEmpty()) {
            QRectF b = recButtonRect(armed);
            if (b.adjusted(-3, -3, 3, 3).contains(pos)) { emit recordButtonClicked(armed); return; }
            if (gainRect(armed).adjusted(-4, -8, 4, 8).contains(pos)) {
                mode = GainDrag;
                inGain = qBound(0.0, (pos.x() - 8) / (HEAD_W - 16), 1.0);
                emit inputGainChanged(inGain); update();
            }
        }
        return;
    }
    if (blade) {                                                  // ножницы: режем там, где курсор
        ClipPtr c = clipAt(pos);
        if (c && e->button() == Qt::LeftButton) {
            emit aboutToChange();
            project->split(c, snapTime(x2t(pos.x()), QList<double>() << 0.0, QList<ClipPtr>()));
            emit changed(); update();
        }
        return;
    }
    // ✕ на плашках эффектов/анимаций
    for (const Chip& ch : chips) {
        if (!ch.close.contains(pos)) continue;
        ClipPtr c = clipById(ch.clipId);
        if (!c) return;
        emit aboutToChange();
        if (ch.kind == 0 && ch.index < (int)c->effects.size()) c->effects.erase(c->effects.begin() + ch.index);
        else if (ch.kind == 1) c->animIn.clear();
        else if (ch.kind == 2) c->animOut.clear();
        else if (ch.kind == 3) c->animLoop.clear();
        selectClip(c);
        emit changed(); update();
        return;
    }
    // шарики затухания / анимаций
    ClipPtr hc = clipAt(pos);
    if (hc) {
        for (int w = 0; w < 4; ++w) {
            if (!handleVisible(hc, w)) continue;
            QPointF h = handlePos(hc, w);
            if (std::abs(pos.x() - h.x()) <= 8 && std::abs(pos.y() - h.y()) <= 8) {
                mode = w == 0 ? FadeIn : w == 1 ? FadeOut : w == 2 ? AnimIn : AnimOut;
                lead = hc;
                selected.clear(); selected.append(hc);
                emit selectionChanged(hc);
                update();
                return;
            }
        }
    }
    if (std::abs(pos.x() - t2x(playhead)) <= 8 || pos.y() < RULER_H || e->button() == Qt::MiddleButton) {
        mode = Scrub; scrubTo(pos.x()); return;                   // красную линию можно хватать где угодно
    }
    ClipPtr c = clipAt(pos);
    if (!c) { selected.clear(); emit selectionChanged(ClipPtr()); mode = Scrub; scrubTo(pos.x()); update(); return; }
    QRectF r = clipRect(c);
    mode = Move;
    if (r.width() > 24) {
        if (pos.x() - r.left() <= 6) mode = TrimL;
        else if (r.right() - pos.x() <= 6) mode = TrimR;
    }
    if (!(e->modifiers() & Qt::ShiftModifier)) selected.clear();
    for (const ClipPtr& g : project->group(c)) if (!selected.contains(g)) selected.append(g);
    lead = c; dragT0 = x2t(pos.x());
    orig.clear();
    for (const ClipPtr& g : selected) { Orig o; o.start = g->start; o.in = g->in; o.dur = g->dur; o.track = g->track; orig[g->id] = o; }
    emit selectionChanged(c);
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* e) {
    QPointF pos = e->position();
    mouseX = pos.x();
    if (mode == None) {
        if (blade) setCursor(Qt::CrossCursor);
        else if (std::abs(pos.x() - t2x(playhead)) <= 8) setCursor(Qt::SizeHorCursor);
        else {
            ClipPtr c = clipAt(pos);
            bool edge = false, hnd = false;
            if (c) {
                QRectF r = clipRect(c);
                edge = r.width() > 24 && (pos.x() - r.left() <= 6 || r.right() - pos.x() <= 6);
                for (int w = 0; w < 4; ++w) {
                    if (!handleVisible(c, w)) continue;
                    QPointF h = handlePos(c, w);
                    if (std::abs(pos.x() - h.x()) <= 8 && std::abs(pos.y() - h.y()) <= 8) hnd = true;
                }
            }
            setCursor(hnd ? Qt::PointingHandCursor : edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
        if (blade) update();
        return;
    }
    if (mode == Scrub) { scrubTo(pos.x()); return; }
    if (mode == GainDrag) {
        inGain = qBound(0.0, (pos.x() - 8) / (HEAD_W - 16), 1.0);
        emit inputGainChanged(inGain); update();
        return;
    }
    beginEdit();
    if (mode == FadeIn || mode == FadeOut || mode == AnimIn || mode == AnimOut) {
        QRectF r = clipRect(lead);
        if (mode == FadeIn) lead->fadeIn = qBound(0.0, (pos.x() - r.left()) / pps, qMax(lead->dur - lead->fadeOut, 0.0));
        else if (mode == FadeOut) lead->fadeOut = qBound(0.0, (r.right() - pos.x()) / pps, qMax(lead->dur - lead->fadeIn, 0.0));
        else if (mode == AnimIn) lead->animInDur = qBound(0.05, (pos.x() - r.left()) / pps, lead->dur);
        else lead->animOutDur = qBound(0.05, (r.right() - pos.x()) / pps, lead->dur);
        emit changed(); update();
        return;
    }
    double dt = x2t(pos.x()) - dragT0;
    if (mode == Move) {
        double lead0 = orig[lead->id].start;
        double nl = snapTime(qMax(lead0 + dt, 0.0), QList<double>() << 0.0 << lead->dur, selected);
        double delta = nl - lead0;
        for (const ClipPtr& g : selected) g->start = qMax(orig[g->id].start + delta, 0.0);
        QString nt = y2track(pos.y());                              // переносим между дорожками того же типа
        if (isVideoTrack(nt) == isVideoTrack(lead->track)) lead->track = nt;
    } else if (mode == TrimL) {
        for (const ClipPtr& g : selected) {
            const Orig& o = orig[g->id];
            double d = qBound(-o.in, dt, o.dur - 0.05);
            if (o.start + d < 0) d = -o.start;
            g->start = o.start + d; g->in = o.in + d; g->dur = o.dur - d;
        }
    } else if (mode == TrimR) {
        for (const ClipPtr& g : selected) {
            const Orig& o = orig[g->id];
            double nd = qMax(o.dur + dt, 0.05);
            auto ai = project->assets.constFind(g->assetId);
            if ((g->kind == "video" || g->kind == "audio") && ai != project->assets.constEnd())
                nd = qMin(nd, qMax(ai.value().duration - o.in, 0.05));
            g->dur = nd;
        }
    }
    emit changed();
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) { mode = None; lead.reset(); }
void TimelineWidget::leaveEvent(QEvent*) { mouseX = -1; update(); }

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    ClipPtr c = clipAt(e->position());
    if (c && c->kind == "text") emit textEditRequested(c);
}

void TimelineWidget::wheelEvent(QWheelEvent* e) {
    if (e->modifiers() & Qt::ControlModifier) {
        double t = x2t(e->position().x());
        pps = qBound(5.0, pps * (e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15), 1000.0);
        scroll = qMax(t - (e->position().x() - HEAD_W) / pps, 0.0);
    } else {
        scroll = qMax(scroll - e->angleDelta().y() / pps * 0.6, 0.0);
    }
    update();
}

void TimelineWidget::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) setBlade(false);
    else QWidget::keyPressEvent(e);
}

// ---------- контекстное меню (ПКМ) ----------
void TimelineWidget::contextMenuEvent(QContextMenuEvent* e) {
    QPointF pos = e->pos();
    ClipPtr c = clipAt(pos);
    bool inTracks = pos.y() >= RULER_H;
    QString track = inTracks ? y2track(pos.y()) : QString();
    double t = x2t(pos.x());
    QMenu menu(this);
    QAction *aSplit = nullptr, *aCopy = nullptr, *aCut = nullptr, *aDup = nullptr, *aDel = nullptr, *aRip = nullptr, *aEdit = nullptr;
    QMap<QAction*, int> fxActs, animActs;
    if (c) {
        aSplit = menu.addAction("✂ Разрезать здесь");
        aCopy = menu.addAction("Копировать");
        aCut = menu.addAction("Вырезать");
        aDup = menu.addAction("Дублировать");
        aDel = menu.addAction("Удалить");
        aRip = menu.addAction("Удалить со сдвигом");
        if (c->kind == "text") aEdit = menu.addAction("Изменить текст…");
        if (!c->effects.empty()) {
            QMenu* m = menu.addMenu("Убрать эффект");
            for (int i = 0; i < (int)c->effects.size(); ++i) fxActs[m->addAction(c->effects[i].name)] = i;
        }
        if (!c->animIn.isEmpty() || !c->animOut.isEmpty() || !c->animLoop.isEmpty()) {
            QMenu* m = menu.addMenu("Убрать анимацию");
            if (!c->animIn.isEmpty()) animActs[m->addAction("▶ " + c->animIn)] = 1;
            if (!c->animOut.isEmpty()) animActs[m->addAction("◀ " + c->animOut)] = 2;
            if (!c->animLoop.isEmpty()) animActs[m->addAction("∞ " + c->animLoop)] = 3;
        }
        menu.addSeparator();
    }
    QAction* aPaste = menu.addAction("Вставить");
    QAction* aText = menu.addAction("T  Добавить текст сюда");
    QAction *aRec = nullptr, *aUnarm = nullptr;
    if (inTracks && !isVideoTrack(track)) {
        menu.addSeparator();
        aRec = menu.addAction("🎙 Записать голос");
        if (armed == track) aUnarm = menu.addAction("Закрыть панель записи");
    }
    menu.addSeparator();
    QAction* aScr = menu.addAction("🖥 Записать экран");
    QAction* aAdd = menu.addAction("➕ Добавить дорожку…");
    QAction* aRemTrack = nullptr;
    if (inTracks) aRemTrack = menu.addAction("Удалить дорожку " + track);

    QAction* res = menu.exec(e->globalPos());
    if (!res) return;
    if (res == aSplit) { emit aboutToChange(); project->split(c, snapTime(t, QList<double>() << 0.0, QList<ClipPtr>())); emit changed(); }
    else if (res == aCopy) emit copyRequested();
    else if (res == aCut) emit cutRequested();
    else if (res == aDup) emit duplicateRequested();
    else if (res == aDel) deleteSelected(false);
    else if (res == aRip) deleteSelected(true);
    else if (res == aEdit) emit textEditRequested(c);
    else if (fxActs.contains(res)) {
        emit aboutToChange();
        int i = fxActs[res];
        if (i < (int)c->effects.size()) c->effects.erase(c->effects.begin() + i);
        selectClip(c); emit changed();
    } else if (animActs.contains(res)) {
        emit aboutToChange();
        int k = animActs[res];
        if (k == 1) c->animIn.clear(); else if (k == 2) c->animOut.clear(); else c->animLoop.clear();
        selectClip(c); emit changed();
    }
    else if (res == aPaste) emit pasteRequested(t);
    else if (res == aText) emit addTextRequested(t);
    else if (res == aScr) emit recordScreenRequested();
    else if (res == aRec) { armed = track; emit recordVoiceRequested(track); }
    else if (res == aUnarm) { armed.clear(); }
    else if (res == aAdd) {
        bool ok = false;                                            // окошко выбора типа дорожки
        QString kind = QInputDialog::getItem(this, "Добавить дорожку", "Какую дорожку добавить?",
                                             QStringList() << "Видео дорожка" << "Аудио (звуковая) дорожка", 0, false, &ok);
        if (ok) {
            emit aboutToChange();
            project->addTrack(kind.startsWith("Видео"));
            refreshTracks();
            emit tracksChanged();
            emit changed();
        }
    } else if (res == aRemTrack) {
        emit aboutToChange();
        if (project->removeTrack(track)) { if (armed == track) armed.clear(); refreshTracks(); emit tracksChanged(); emit changed(); }
    }
    update();
}

// ---------- операции ----------
void TimelineWidget::splitAtPlayhead() {
    QList<ClipPtr> targets = selected.isEmpty() ? project->clipsAt(playhead) : selected;
    emit aboutToChange();
    QList<int> done;
    for (const ClipPtr& c : targets) {
        if (done.contains(c->id)) continue;
        for (const ClipPtr& r : project->split(c, playhead)) done << r->id;
    }
    emit changed(); update();
}

void TimelineWidget::addMarker() {
    static const char* cols[] = {"#e5484d", "#f5a524", "#46a758", "#3e63dd", "#8e4ec6"};
    emit aboutToChange();
    Marker m; m.t = playhead; m.color = cols[project->markers.size() % 5];
    project->markers.append(m);
    update();
}

void TimelineWidget::deleteSelected(bool ripple) {
    if (selected.isEmpty()) return;
    emit aboutToChange();
    if (ripple) project->rippleDelete(selected); else project->remove(selected);
    selected.clear();
    emit selectionChanged(ClipPtr());
    emit changed(); update();
}

void TimelineWidget::jumpCut(int dir) {
    double best = dir < 0 ? 0.0 : project->duration();
    for (const ClipPtr& c : project->clips)
        for (double e : {c->start, c->end()}) {
            if (dir < 0 && e < playhead - 0.01 && e > best) best = e;
            if (dir > 0 && e > playhead + 0.01 && e < best) best = e;
        }
    setPlayhead(best);
    emit playheadDragged(best);
}

// ---------- drag & drop из файлового менеджера ----------
void TimelineWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat("application/x-uve-effect")) e->acceptProposedAction();
}
void TimelineWidget::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat("application/x-uve-effect")) {          // подсвечиваем клип, на который упадёт эффект
        ClipPtr c = clipAt(e->position());
        int id = c ? c->id : -1;
        if (id != dropHighlight) { dropHighlight = id; update(); }
    }
    e->acceptProposedAction();
}
void TimelineWidget::dragLeaveEvent(QDragLeaveEvent*) { dropHighlight = -1; update(); }
void TimelineWidget::dropEvent(QDropEvent* e) {
    if (e->mimeData()->hasFormat("application/x-uve-effect")) {
        ClipPtr c = clipAt(e->position());
        dropHighlight = -1;
        if (c) emit effectDropped(c, QString::fromUtf8(e->mimeData()->data("application/x-uve-effect")));
        update();
        e->acceptProposedAction();
        return;
    }
    QStringList paths;
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile() && !mediaKind(u.toLocalFile()).isEmpty()) paths << u.toLocalFile();
    if (paths.isEmpty()) return;
    double t = snapTime(x2t(e->position().x()), QList<double>() << 0.0, QList<ClipPtr>());
    emit filesDropped(paths, t, y2track(e->position().y()));
    e->acceptProposedAction();
}
