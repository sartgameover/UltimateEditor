#include "MainWindow.h"
#include "StartScreen.h"
#include "Installer.h"
#include "Branding.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QPalette>
#include <QFileInfo>

static void darkTheme(QApplication& app) {
    app.setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window, QColor("#2b2d31"));
    p.setColor(QPalette::WindowText, QColor("#e3e5e8"));
    p.setColor(QPalette::Base, QColor("#1e1f22"));
    p.setColor(QPalette::AlternateBase, QColor("#2b2d31"));
    p.setColor(QPalette::Text, QColor("#e3e5e8"));
    p.setColor(QPalette::Button, QColor("#383a40"));
    p.setColor(QPalette::ButtonText, QColor("#e3e5e8"));
    p.setColor(QPalette::Highlight, QColor("#3b6ea5"));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    p.setColor(QPalette::ToolTipBase, QColor("#1e1f22"));
    p.setColor(QPalette::ToolTipText, QColor("#e3e5e8"));
    app.setPalette(p);
}

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setOrganizationName("UltimateEditor");
    app.setApplicationName("UltimateEditor");
    darkTheme(app);
    const QStringList args = app.arguments();

    // служебные команды (для сборки AppImage и удаления)
    int ei = args.indexOf("--export-icon");
    if (ei >= 0 && ei + 1 < args.size()) { brandIcon(256).save(args[ei + 1], "PNG"); return 0; }
    if (args.contains("--uninstall")) { InstallerDialog::uninstall(); return 0; }

    // красивый установщик (AppImage при первом запуске)
    if (InstallerDialog::shouldRun(args)) {
        InstallerDialog inst;
        if (inst.exec() != QDialog::Accepted) return 0;
    }

    // что открыть: .uvmvideos (двойной клик по файлу), медиафайлы или показать стартовое окно
    QString projectFile;
    QStringList media;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        if (a.startsWith("--") || !QFileInfo::exists(a)) continue;
        if (a.endsWith(".uvmvideos")) projectFile = a; else media << a;
    }
    QString chosen = projectFile;
    if (projectFile.isEmpty() && media.isEmpty()) {
        StartScreen ss;                                           // мини-окно с Baner / BanrIcon, недавние и найденные проекты
        if (ss.exec() != QDialog::Accepted) return 0;
        chosen = ss.chosenPath();
    }

    MainWindow w;
    if (!chosen.isEmpty()) w.openProjectFile(chosen);
    w.show();
    if (!media.isEmpty()) w.importPaths(media, 0, "V1", true);
    return app.exec();
}
