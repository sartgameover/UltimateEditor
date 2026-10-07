#pragma once
#include <QPixmap>
#include <QStringList>

// Картинки из папки Image: Baner (фон стартового окна) и BanrIcon (значок). Ищутся рядом с программой,
// в проекте и внутри AppImage; расширение любое (png/jpg/webp/svg…). Если файла нет — рисуется запасной вариант.
QPixmap brandBanner();
QPixmap brandIcon(int size = 256);

// Недавние проекты
QStringList recentProjects();
void addRecentProject(const QString& path);
void removeRecentProject(const QString& path);
