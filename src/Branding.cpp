#include <QPolygonF>
#include "Branding.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QLinearGradient>
#include <QSettings>

static QString findImage(const QString& baseName) {
    QStringList dirs;
    QString app = QCoreApplication::applicationDirPath();
    dirs << app + "/Image" << app + "/../Image" << app + "/../../Image" << app + "/../share/uve/Image"
         << QDir::currentPath() + "/Image";
#ifdef UVE_SOURCE_DIR
    dirs << QString(UVE_SOURCE_DIR) + "/Image";
#endif
    static const QStringList exts = {"png", "jpg", "jpeg", "webp", "bmp", "svg"};
    for (const QString& d : dirs) {
        QDir dir(d);
        if (!dir.exists()) continue;
        for (const QFileInfo& fi : dir.entryInfoList(QDir::Files))
            if (fi.completeBaseName().compare(baseName, Qt::CaseInsensitive) == 0 && exts.contains(fi.suffix().toLower()))
                return fi.absoluteFilePath();
    }
    return QString();
}

QPixmap brandBanner() {
    QString f = findImage("Baner");
    if (f.isEmpty()) f = findImage("Banner");
    QPixmap px(f);
    if (!px.isNull()) return px;
    QPixmap g(1200, 700);                                       // запасной фон
    QPainter p(&g);
    QLinearGradient lg(0, 0, 1200, 700);
    lg.setColorAt(0, QColor("#141a2e")); lg.setColorAt(0.5, QColor("#2a1f4d")); lg.setColorAt(1, QColor("#0f3b57"));
    p.fillRect(g.rect(), lg);
    return g;
}

QPixmap brandIcon(int size) {
    QString f = findImage("BanrIcon");
    if (f.isEmpty()) f = findImage("BannerIcon");
    QPixmap px(f);
    if (!px.isNull()) return px.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap g(size, size);                                      // запасной значок: красный круг с «play»
    g.fill(Qt::transparent);
    QPainter p(&g);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor("#e5484d")); p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(size * 0.05, size * 0.05, size * 0.9, size * 0.9));
    p.setBrush(Qt::white);
    QPolygonF tri; tri << QPointF(size * 0.40, size * 0.28) << QPointF(size * 0.40, size * 0.72) << QPointF(size * 0.74, size * 0.5);
    p.drawPolygon(tri);
    return g;
}

QStringList recentProjects() {
    QStringList all = QSettings("UltimateEditor", "UltimateEditor").value("recent_projects").toStringList(), ok;
    for (const QString& p : all) if (QFileInfo::exists(p)) ok << p;
    return ok;
}

void addRecentProject(const QString& path) {
    QSettings s("UltimateEditor", "UltimateEditor");
    QStringList l = s.value("recent_projects").toStringList();
    l.removeAll(path); l.prepend(path);
    while (l.size() > 12) l.removeLast();
    s.setValue("recent_projects", l);
}

void removeRecentProject(const QString& path) {
    QSettings s("UltimateEditor", "UltimateEditor");
    QStringList l = s.value("recent_projects").toStringList();
    l.removeAll(path);
    s.setValue("recent_projects", l);
}
