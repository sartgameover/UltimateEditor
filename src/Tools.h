#pragma once
#include <QString>
#include <QCoreApplication>
#include <QFileInfo>

// ffmpeg из AppImage (рядом с программой) или системный
inline QString ffmpegPath(const char* name = "ffmpeg") {
    QString p = QCoreApplication::applicationDirPath() + "/" + name;
    return QFileInfo::exists(p) ? p : QString(name);
}
