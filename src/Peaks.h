#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>

// Пики громкости (50 значений в секунду): волновые формы и «умные» шейдеры.
class Peaks : public QObject {
    Q_OBJECT
public:
    static const int PER_SEC = 50;
    static Peaks& instance();
    const QVector<float>* get(const QString& path) const;     // nullptr, пока не посчитано
    void request(const QString& path);                         // асинхронно через ffmpeg
    float level(const QString& path, double t) const;
signals:
    void ready(const QString& path);
private:
    QHash<QString, QVector<float>> data;
    QSet<QString> pending;
};
