#include "VoiceRecorder.h"
#include <QAudioSource>
#include <QAudioDevice>
#include <QIODevice>
#include <cstdint>
#include <cmath>

bool VoiceRecorder::start(const QAudioDevice& dev, const QString& path) {
    if (src) return false;
    QAudioFormat f;
    f.setSampleRate(48000); f.setChannelCount(1); f.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(f)) {
        f.setSampleRate(44100);
        if (!dev.isFormatSupported(f)) f = dev.preferredFormat();
    }
    if (f.sampleFormat() != QAudioFormat::Int16 && f.sampleFormat() != QAudioFormat::Float) {
        emit failed("Формат микрофона не поддерживается");
        return false;
    }
    fmt = f;
    file.setFileName(path);
    if (!file.open(QIODevice::WriteOnly)) { emit failed("Не удалось создать файл записи"); return false; }
    file.write(QByteArray(44, '\0'));                              // место под WAV-заголовок
    bytes = 0;
    src = new QAudioSource(dev, fmt, this);
    io = src->start();
    if (!io) { delete src; src = nullptr; file.close(); emit failed("Не удалось открыть микрофон"); return false; }
    connect(io, &QIODevice::readyRead, this, &VoiceRecorder::onReady);
    return true;
}

void VoiceRecorder::onReady() {
    if (!io) return;
    QByteArray d = io->readAll();
    if (d.isEmpty()) return;
    QByteArray out;
    float peak = 0.f;
    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        int n = d.size() / 2;
        out.resize(n * 2);
        const int16_t* in = reinterpret_cast<const int16_t*>(d.constData());
        int16_t* o = reinterpret_cast<int16_t*>(out.data());
        for (int i = 0; i < n; ++i) {
            int v = (int)std::lround(in[i] * gain);
            v = qBound(-32768, v, 32767);
            o[i] = (int16_t)v;
            peak = qMax(peak, std::abs(v) / 32768.f);
        }
    } else {                                                        // Float -> Int16
        int n = d.size() / 4;
        out.resize(n * 2);
        const float* in = reinterpret_cast<const float*>(d.constData());
        int16_t* o = reinterpret_cast<int16_t*>(out.data());
        for (int i = 0; i < n; ++i) {
            float x = qBound(-1.0f, (float)(in[i] * gain), 1.0f);
            o[i] = (int16_t)std::lround(x * 32767.0f);
            peak = qMax(peak, std::abs(x));
        }
    }
    file.write(out);
    bytes += out.size();
    emit level(peak);
}

static void put32(QByteArray& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.append((char)((v >> (8 * i)) & 0xFF)); }
static void put16(QByteArray& b, uint16_t v) { for (int i = 0; i < 2; ++i) b.append((char)((v >> (8 * i)) & 0xFF)); }

void VoiceRecorder::stop() {
    if (!src) return;
    onReady();
    src->stop();
    onReady();
    if (io) disconnect(io, nullptr, this, nullptr);
    src->deleteLater();
    src = nullptr; io = nullptr;
    int ch = fmt.channelCount(), sr = fmt.sampleRate();
    QByteArray h;
    h.append("RIFF"); put32(h, (uint32_t)(36 + bytes)); h.append("WAVE");
    h.append("fmt "); put32(h, 16); put16(h, 1); put16(h, (uint16_t)ch); put32(h, (uint32_t)sr);
    put32(h, (uint32_t)(sr * ch * 2)); put16(h, (uint16_t)(ch * 2)); put16(h, 16);
    h.append("data"); put32(h, (uint32_t)bytes);
    file.seek(0);
    file.write(h);
    file.close();
    emit finished(file.fileName(), bytes / (double)qMax(sr * ch * 2, 1));
}
