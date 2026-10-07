#pragma once
#include <QString>
#include <vector>
#include <cstdint>

struct AVFormatContext; struct AVCodecContext; struct SwsContext; struct SwrContext; struct AVFrame; struct AVPacket;

struct MediaInfo { double duration = 5; bool hasAudio = false; int w = 0, h = 0; };
MediaInfo probeMedia(const QString& path);

// Видеодекодер на libavcodec: многопоточное декодирование, быстрый sequential-режим, seek при скраббинге.
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    bool open(const QString& path, int maxWidth);
    bool isOpen() const { return ctx != nullptr && frame != nullptr; }
    // Кадр, покрывающий момент t. isNew == true, если кадр сменился с прошлого вызова.
    bool frameAt(double t, const uint8_t*& data, int& w, int& h, bool& isNew);
private:
    void close();
    bool seekTo(double t);
    bool decodeUntil(double t);
    void convert();
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int vstream = -1;
    double timeBase = 0, fps = 30, frameDur = 1.0 / 30, curPts = -1e9, startOffset = 0;
    int outW = 0, outH = 0;
    bool have = false, eof = false;
    std::vector<uint8_t> rgba;
};

// Аудиодекодер: любой формат -> float, стерео, 48 кГц (libavcodec + libswresample). Последовательное чтение быстрое, seek — по времени.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();
    bool open(const QString& path);
    bool isOpen() const { return ctx != nullptr; }
    int read(double srcTime, float* out, int frames);          // возвращает число готовых кадров (остальное — тишина)
private:
    void close();
    bool initSwr();
    bool seekTo(double t);
    bool decodeMore();
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int astream = -1;
    double timeBase = 0, startOffset = 0, readPos = 0;
    bool haveData = false, eof = false;
    std::vector<float> buf;
    size_t head = 0;
};
