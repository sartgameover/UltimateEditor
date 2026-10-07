#include "Decoder.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#include <cmath>
#include <cstring>
#include <algorithm>

MediaInfo probeMedia(const QString& path) {
    MediaInfo mi;
    AVFormatContext* f = nullptr;
    if (avformat_open_input(&f, path.toUtf8().constData(), nullptr, nullptr) < 0) return mi;
    if (avformat_find_stream_info(f, nullptr) >= 0) {
        if (f->duration > 0) mi.duration = f->duration / (double)AV_TIME_BASE;
        for (unsigned i = 0; i < f->nb_streams; ++i) {
            AVCodecParameters* p = f->streams[i]->codecpar;
            if (p->codec_type == AVMEDIA_TYPE_AUDIO) mi.hasAudio = true;
            else if (p->codec_type == AVMEDIA_TYPE_VIDEO && mi.w == 0) { mi.w = p->width; mi.h = p->height; }
        }
    }
    avformat_close_input(&f);
    return mi;
}

VideoDecoder::VideoDecoder() {}
VideoDecoder::~VideoDecoder() { close(); }

void VideoDecoder::close() {
    if (sws) { sws_freeContext(sws); sws = nullptr; }
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (ctx) avcodec_free_context(&ctx);
    if (fmt) avformat_close_input(&fmt);
    have = false;
}

bool VideoDecoder::open(const QString& path, int maxWidth) {
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) { fmt = nullptr; return false; }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { close(); return false; }
    vstream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        AVStream* s = fmt->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) { vstream = (int)i; break; }
    }
    if (vstream < 0) { close(); return false; }
    AVStream* st = fmt->streams[vstream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { close(); return false; }
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) { close(); return false; }
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->thread_count = 0;                     // авто: все ядра
    if (avcodec_open2(ctx, codec, nullptr) < 0) { close(); return false; }
    timeBase = av_q2d(st->time_base);
    startOffset = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * timeBase : 0.0;
    AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
    fps = (fr.num > 0 && fr.den > 0) ? av_q2d(fr) : 30.0;
    if (fps < 1 || fps > 240) fps = 30;
    frameDur = 1.0 / fps;
    int w = ctx->width, h = ctx->height;
    double k = (maxWidth > 0 && w > maxWidth) ? maxWidth / (double)w : 1.0;
    outW = ((int)(w * k) + 1) & ~1;
    outH = ((int)(h * k) + 1) & ~1;
    if (outW < 2 || outH < 2) { close(); return false; }
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    rgba.assign((size_t)outW * outH * 4, 0);
    return frame && pkt;
}

bool VideoDecoder::seekTo(double t) {
    int64_t ts = (int64_t)(t / timeBase);
    AVStream* st = fmt->streams[vstream];
    if (st->start_time != AV_NOPTS_VALUE) ts += st->start_time;
    if (av_seek_frame(fmt, vstream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(ctx);
    have = false; eof = false; curPts = -1e9;
    return true;
}

void VideoDecoder::convert() {
    sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format,
                               outW, outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) return;
    uint8_t* dst[4] = { rgba.data(), nullptr, nullptr, nullptr };
    int ls[4] = { outW * 4, 0, 0, 0 };
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, ls);
}

bool VideoDecoder::decodeUntil(double t) {
    bool got = false;
    while (true) {
        int r = avcodec_receive_frame(ctx, frame);
        if (r == 0) {
            int64_t bts = frame->best_effort_timestamp;
            double pts = (bts == AV_NOPTS_VALUE) ? (curPts > -1e8 ? curPts + frameDur : 0.0) : bts * timeBase - startOffset;
            curPts = pts; got = true;
            if (pts + frameDur > t + 1e-4) break;       // этот кадр покрывает t
            continue;                                  // иначе выбрасываем без конвертации
        }
        if (r == AVERROR(EAGAIN)) {
            if (eof) break;
            int rr = av_read_frame(fmt, pkt);
            if (rr < 0) { eof = true; avcodec_send_packet(ctx, nullptr); continue; }
            if (pkt->stream_index == vstream) avcodec_send_packet(ctx, pkt);
            av_packet_unref(pkt);
            continue;
        }
        break;                                         // EOF или ошибка
    }
    if (got) { convert(); have = true; }
    return got;
}

bool VideoDecoder::frameAt(double t, const uint8_t*& data, int& w, int& h, bool& isNew) {
    isNew = false;
    if (!isOpen()) return false;
    if (t < 0) t = 0;
    if (have && t >= curPts - 1e-4 && t < curPts + frameDur - 1e-4) {
        data = rgba.data(); w = outW; h = outH; return true;
    }
    bool needSeek = !have || t < curPts - 1e-4 || t > curPts + 1.0;
    if (needSeek) seekTo(t);
    if (decodeUntil(t)) isNew = true;
    if (!have) return false;
    data = rgba.data(); w = outW; h = outH;
    return true;
}

// ====================== AudioDecoder ======================
AudioDecoder::AudioDecoder() {}
AudioDecoder::~AudioDecoder() { close(); }

void AudioDecoder::close() {
    if (swr) swr_free(&swr);
    if (frame) av_frame_free(&frame);
    if (pkt) av_packet_free(&pkt);
    if (ctx) avcodec_free_context(&ctx);
    if (fmt) avformat_close_input(&fmt);
    haveData = false;
}

bool AudioDecoder::initSwr() {
    if (swr) swr_free(&swr);
    AVChannelLayout outL;
    av_channel_layout_default(&outL, 2);
    int r = swr_alloc_set_opts2(&swr, &outL, AV_SAMPLE_FMT_FLT, 48000, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&outL);
    if (r < 0 || !swr) return false;
    return swr_init(swr) >= 0;
}

bool AudioDecoder::open(const QString& path) {
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) { fmt = nullptr; return false; }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { close(); return false; }
    astream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { astream = (int)i; break; }
    if (astream < 0) { close(); return false; }
    AVStream* st = fmt->streams[astream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { close(); return false; }
    ctx = avcodec_alloc_context3(codec);
    if (!ctx) { close(); return false; }
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (avcodec_open2(ctx, codec, nullptr) < 0) { close(); return false; }
    if (ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&ctx->ch_layout, ctx->ch_layout.nb_channels > 0 ? ctx->ch_layout.nb_channels : 2);
    timeBase = av_q2d(st->time_base);
    startOffset = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * timeBase : 0.0;
    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt || !initSwr()) { close(); return false; }
    return true;
}

bool AudioDecoder::seekTo(double t) {
    AVStream* st = fmt->streams[astream];
    int64_t ts = (int64_t)(t / timeBase);
    if (st->start_time != AV_NOPTS_VALUE) ts += st->start_time;
    if (av_seek_frame(fmt, astream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(ctx);
    eof = false; haveData = false; buf.clear(); head = 0;
    return initSwr();
}

bool AudioDecoder::decodeMore() {
    while (true) {
        int r = avcodec_receive_frame(ctx, frame);
        if (r == 0) {
            int64_t bts = frame->best_effort_timestamp;
            double avail = (double)(buf.size() - head) / 2.0 / 48000.0;
            double ft = (bts == AV_NOPTS_VALUE) ? readPos + avail : bts * timeBase - startOffset;
            int maxOut = swr_get_out_samples(swr, frame->nb_samples);
            if (maxOut <= 0) return true;
            std::vector<float> tmp((size_t)maxOut * 2 + 16);
            uint8_t* outp[1] = { reinterpret_cast<uint8_t*>(tmp.data()) };
            int n = swr_convert(swr, outp, maxOut, (const uint8_t**)frame->extended_data, frame->nb_samples);
            if (n > 0) {
                if (!haveData) { readPos = ft; buf.clear(); head = 0; haveData = true; }
                buf.insert(buf.end(), tmp.begin(), tmp.begin() + (size_t)n * 2);
            }
            return true;
        }
        if (r == AVERROR(EAGAIN)) {
            if (eof) return false;
            int rr = av_read_frame(fmt, pkt);
            if (rr < 0) { eof = true; avcodec_send_packet(ctx, nullptr); continue; }
            if (pkt->stream_index == astream) avcodec_send_packet(ctx, pkt);
            av_packet_unref(pkt);
            continue;
        }
        return false;
    }
}

int AudioDecoder::read(double t, float* out, int frames) {
    if (!ctx) return 0;
    const double sr = 48000.0;
    auto avail = [&]() { return (int)((buf.size() - head) / 2); };
    if (!haveData || t < readPos - 0.003 || t > readPos + 3.0) {
        if (!seekTo(t)) return 0;
        while (!haveData && decodeMore()) {}
        if (!haveData) return 0;
    }
    long skip = (long)((t - readPos) * sr + 0.5);              // дойти до нужного момента
    while (skip > 0) {
        if (avail() == 0 && !decodeMore()) break;
        int n = (int)std::min<long>(avail(), skip);
        head += (size_t)n * 2; readPos += n / sr; skip -= n;
    }
    while (avail() < frames && decodeMore()) {}
    int n = std::min(avail(), frames);
    if (n > 0) {
        std::memcpy(out, buf.data() + head, (size_t)n * 2 * sizeof(float));
        head += (size_t)n * 2; readPos += n / sr;
    }
    if (head > 192000) { buf.erase(buf.begin(), buf.begin() + (long)head); head = 0; }
    return n;
}
