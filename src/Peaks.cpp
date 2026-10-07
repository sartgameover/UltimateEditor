#include "Peaks.h"
#include "Tools.h"
#include <QProcess>
#include <algorithm>
#include <cstdint>
#include <vector>

Peaks& Peaks::instance() { static Peaks p; return p; }

const QVector<float>* Peaks::get(const QString& path) const {
    auto it = data.constFind(path);
    return it == data.constEnd() ? nullptr : &it.value();
}

float Peaks::level(const QString& path, double t) const {
    const QVector<float>* p = get(path);
    if (!p || p->isEmpty() || t < 0) return 0.f;
    int i = (int)(t * PER_SEC);
    float m = 0.f;
    for (int k = i; k < i + 3 && k < p->size(); ++k) m = qMax(m, p->at(k));
    return m;
}

void Peaks::request(const QString& path) {
    if (data.contains(path) || pending.contains(path)) return;
    pending.insert(path);
    QProcess* pr = new QProcess(this);
    connect(pr, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, pr, path](int, QProcess::ExitStatus) {
        QByteArray raw = pr->readAllStandardOutput();
        const int16_t* s = reinterpret_cast<const int16_t*>(raw.constData());
        int n = raw.size() / 2, step = 8000 / PER_SEC, cnt = n / step;
        QVector<float> pk(qMax(cnt, 1), 0.f);
        for (int i = 0; i < cnt; ++i) {
            int m = 0;
            for (int k = 0; k < step; ++k) m = qMax(m, (int)qAbs((int)s[i * step + k]));
            pk[i] = (float)m;
        }
        if (cnt > 0) {
            std::vector<float> tmp(pk.begin(), pk.end());
            size_t idx = (size_t)(tmp.size() * 0.995);
            if (idx >= tmp.size()) idx = tmp.size() - 1;
            std::nth_element(tmp.begin(), tmp.begin() + idx, tmp.end());
            float norm = qMax(tmp[idx], 1.0f);
            for (float& v : pk) v = qMin(v / norm, 1.0f);
        }
        data[path] = pk;
        pending.remove(path);
        pr->deleteLater();
        emit ready(path);
    });
    pr->start(ffmpegPath(), QStringList() << "-v" << "error" << "-i" << path << "-vn" << "-ac" << "1"
                                      << "-ar" << "8000" << "-f" << "s16le" << "-");
}
