// Android: NDK (libmediandk). AMediaExtractor separa el video del MP4 y AMediaCodec (decodificador por hardware o por software
// del telefono) lo decodifica a buffers YUV 4:2:0 que aqui se convierten a RGBA. Funciona con H.264 y, segun el telefono, HEVC/VP9/AV1.
#ifdef __ANDROID__
#include <fcntl.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <string>
#include "videosource.hpp"

namespace vid {
namespace {

// Claves de AMediaFormat como texto (las constantes AMEDIAFORMAT_KEY_* solo existen desde la API 29; el texto vale en todas)
constexpr const char* kMime = "mime";
constexpr const char* kWidth = "width";
constexpr const char* kHeight = "height";
constexpr const char* kStride = "stride";
constexpr const char* kSlice = "slice-height";
constexpr const char* kColorFormat = "color-format";
constexpr const char* kColorRange = "color-range";          // 1 = completo, 2 = limitado
constexpr const char* kColorStandard = "color-standard";    // 1 = BT.709, 2 = BT.601 PAL, 4 = BT.601 NTSC, 6 = BT.2020
constexpr const char* kCropL = "crop-left";
constexpr const char* kCropT = "crop-top";
constexpr const char* kCropR = "crop-right";
constexpr const char* kCropB = "crop-bottom";

constexpr int32_t kYuv420Planar = 19;                       // I420
constexpr int32_t kYuv420SemiPlanar = 21;                   // NV12
constexpr int32_t kYuv420Flexible = 0x7F420888;

struct OutFmt {
    int w = 0, h = 0, stride = 0, slice = 0, color = kYuv420Flexible;
    int cropL = 0, cropT = 0, cropR = -1, cropB = -1;       // incluyentes (como en Android)
    int range = 2, standard = 0;
};

OutFmt readOutFmt(AMediaFormat* f, int fallbackW, int fallbackH) {
    OutFmt o;
    int32_t v = 0;
    o.w = AMediaFormat_getInt32(f, kWidth, &v) ? v : fallbackW;
    o.h = AMediaFormat_getInt32(f, kHeight, &v) ? v : fallbackH;
    o.stride = AMediaFormat_getInt32(f, kStride, &v) && v > 0 ? v : o.w;
    o.slice = AMediaFormat_getInt32(f, kSlice, &v) && v > 0 ? v : o.h;
    if (AMediaFormat_getInt32(f, kColorFormat, &v)) o.color = v;
    if (AMediaFormat_getInt32(f, kColorRange, &v)) o.range = v;
    if (AMediaFormat_getInt32(f, kColorStandard, &v)) o.standard = v;
    int32_t l = 0, t = 0, r = -1, b = -1;
    const bool hasCrop = AMediaFormat_getInt32(f, kCropL, &l) | AMediaFormat_getInt32(f, kCropT, &t) |
                         AMediaFormat_getInt32(f, kCropR, &r) | AMediaFormat_getInt32(f, kCropB, &b);
    if (hasCrop && r >= l && b >= t) { o.cropL = l; o.cropT = t; o.cropR = r; o.cropB = b; }
    else { o.cropL = 0; o.cropT = 0; o.cropR = o.w - 1; o.cropB = o.h - 1; }
    return o;
}

inline uint8_t clamp8(int v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUV 4:2:0 (I420 o NV12) -> RGBA. BT.601 o BT.709 (HD), rango limitado o completo; aritmetica entera (como libyuv).
bool yuvToRgba(const uint8_t* data, size_t size, const OutFmt& f, std::vector<uint8_t>& rgba, int& outW, int& outH) {
    const int x0 = f.cropL, y0 = f.cropT;
    outW = f.cropR - f.cropL + 1; outH = f.cropB - f.cropT + 1;
    if (outW <= 0 || outH <= 0) return false;
    const bool nv12 = f.color == kYuv420SemiPlanar || f.color == kYuv420Flexible;
    const bool i420 = f.color == kYuv420Planar;
    if (!nv12 && !i420) return false;
    const size_t yPlane = size_t(f.stride) * f.slice;
    const size_t need = nv12 ? yPlane + size_t(f.stride) * ((f.slice + 1) / 2)
                             : yPlane + 2 * size_t((f.stride + 1) / 2) * ((f.slice + 1) / 2);
    if (size < need - size_t(f.stride)) return false;       // (el ultimo renglon de croma puede faltar)
    const uint8_t* Y = data;
    const uint8_t* U = nv12 ? data + yPlane : data + yPlane;
    const uint8_t* V = nv12 ? data + yPlane + 1 : data + yPlane + size_t((f.stride + 1) / 2) * ((f.slice + 1) / 2);
    const int cstride = nv12 ? f.stride : (f.stride + 1) / 2;
    const int cstep = nv12 ? 2 : 1;

    const bool bt709 = f.standard == 1 || (f.standard == 0 && f.h >= 720) || f.standard == 6;
    const bool full = f.range == 1;
    // coeficientes en punto fijo de 8 bits
    int cr_r, cb_g, cr_g, cb_b;
    if (bt709) { cr_r = 459; cb_g = 55; cr_g = 136; cb_b = 541; }      // 1.793 0.213 0.533 2.112  (x256)
    else       { cr_r = 409; cb_g = 100; cr_g = 208; cb_b = 516; }     // 1.596 0.391 0.813 2.018
    const int yScale = full ? 256 : 298;                               // 1.164 en rango limitado
    const int yOff = full ? 0 : 16;
    if (full) { if (bt709) { cr_r = 403; cb_g = 48; cr_g = 120; cb_b = 475; } else { cr_r = 359; cb_g = 88; cr_g = 183; cb_b = 454; } }

    rgba.resize(size_t(outW) * outH * 4);
    for (int y = 0; y < outH; ++y) {
        const int sy = y0 + y;
        const uint8_t* yr = Y + size_t(sy) * f.stride + x0;
        const uint8_t* ur = U + size_t(sy / 2) * cstride;
        const uint8_t* vr = V + size_t(sy / 2) * cstride;
        uint8_t* d = &rgba[size_t(y) * outW * 4];
        for (int x = 0; x < outW; ++x, d += 4) {
            const int sx = x0 + x;
            const int yy = (int(yr[x]) - yOff) * yScale;
            const int u = int(ur[(sx / 2) * cstep]) - 128, v = int(vr[(sx / 2) * cstep]) - 128;
            d[0] = clamp8((yy + cr_r * v + 128) >> 8);
            d[1] = clamp8((yy - cb_g * u - cr_g * v + 128) >> 8);
            d[2] = clamp8((yy + cb_b * u + 128) >> 8);
            d[3] = 255;
        }
    }
    return true;
}

struct Guard {
    AMediaExtractor* ex = nullptr; AMediaCodec* codec = nullptr; int fd = -1; AMediaFormat* fmt = nullptr;
    ~Guard() {
        if (codec) { AMediaCodec_stop(codec); AMediaCodec_delete(codec); }
        if (fmt) AMediaFormat_delete(fmt);
        if (ex) AMediaExtractor_delete(ex);
        if (fd >= 0) close(fd);
    }
};
} // namespace

bool decodeNative(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel) {
    Guard g;
    g.fd = open(rq.path.c_str(), O_RDONLY);
    if (g.fd < 0) { err = "Android: no se pudo abrir el archivo"; return false; }
    struct stat st {};
    if (fstat(g.fd, &st) != 0 || st.st_size <= 0) { err = "Android: archivo vacio"; return false; }

    g.ex = AMediaExtractor_new();
    if (!g.ex || AMediaExtractor_setDataSourceFd(g.ex, g.fd, 0, off64_t(st.st_size)) != AMEDIA_OK) { err = "Android: no se pudo leer el MP4"; return false; }

    int track = -1; int srcW = 0, srcH = 0; const char* mimeC = nullptr; std::string mime;
    const size_t nTracks = AMediaExtractor_getTrackCount(g.ex);
    for (size_t i = 0; i < nTracks && track < 0; ++i) {
        AMediaFormat* tf = AMediaExtractor_getTrackFormat(g.ex, i);
        if (AMediaFormat_getString(tf, kMime, &mimeC) && mimeC && std::strncmp(mimeC, "video/", 6) == 0) {
            track = int(i); mime = mimeC;
            AMediaFormat_getInt32(tf, kWidth, &srcW); AMediaFormat_getInt32(tf, kHeight, &srcH);
            g.fmt = tf;                                      // se conserva para configurar el decodificador
        } else AMediaFormat_delete(tf);
    }
    if (track < 0) { err = "Android: el archivo no tiene video"; return false; }
    AMediaExtractor_selectTrack(g.ex, size_t(track));
    if (rq.start > 0.05) AMediaExtractor_seekTo(g.ex, int64_t(rq.start * 1e6), AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);

    g.codec = AMediaCodec_createDecoderByType(mime.c_str());
    if (!g.codec) { err = "Android: este telefono no tiene decodificador para " + mime; return false; }
    AMediaFormat_setInt32(g.fmt, kColorFormat, kYuv420Flexible);
    if (AMediaCodec_configure(g.codec, g.fmt, nullptr, nullptr, 0) != AMEDIA_OK) { err = "Android: no se pudo configurar el decodificador"; return false; }
    if (AMediaCodec_start(g.codec) != AMEDIA_OK) { err = "Android: no se pudo iniciar el decodificador"; return false; }

    OutFmt of = readOutFmt(g.fmt, srcW, srcH);
    bool inputDone = false, outputDone = false;
    int delivered = 0, idleLoops = 0;
    std::vector<uint8_t> rgba;
    while (!outputDone) {
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        if (!inputDone) {
            const ssize_t ii = AMediaCodec_dequeueInputBuffer(g.codec, 2000);
            if (ii >= 0) {
                size_t cap = 0;
                uint8_t* ib = AMediaCodec_getInputBuffer(g.codec, size_t(ii), &cap);
                const ssize_t n = ib ? AMediaExtractor_readSampleData(g.ex, ib, cap) : -1;
                if (n < 0) {
                    AMediaCodec_queueInputBuffer(g.codec, size_t(ii), 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                } else {
                    const int64_t t = AMediaExtractor_getSampleTime(g.ex);
                    AMediaCodec_queueInputBuffer(g.codec, size_t(ii), 0, size_t(n), uint64_t(std::max<int64_t>(t, 0)), 0);
                    AMediaExtractor_advance(g.ex);
                }
            }
        }
        AMediaCodecBufferInfo info;
        const ssize_t oi = AMediaCodec_dequeueOutputBuffer(g.codec, &info, 2000);
        if (oi >= 0) {
            idleLoops = 0;
            size_t osz = 0;
            uint8_t* ob = AMediaCodec_getOutputBuffer(g.codec, size_t(oi), &osz);
            bool cont = true;
            if (ob && info.size > 0) {
                const uint8_t* base = ob + info.offset;
                int w = 0, h = 0;
                if (yuvToRgba(base, size_t(info.size), of, rgba, w, h)) {
                    cont = sink(rgba.data(), w, h, w * 4, PixFmt::RGBA, double(info.presentationTimeUs) / 1e6);
                    ++delivered;
                } else if (delivered == 0) {
                    err = "Android: formato de color del decodificador no soportado (" + std::to_string(of.color) + ")";
                    AMediaCodec_releaseOutputBuffer(g.codec, size_t(oi), false);
                    return false;
                }
            }
            AMediaCodec_releaseOutputBuffer(g.codec, size_t(oi), false);
            if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) || !cont) outputDone = true;
        } else if (oi == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* nf = AMediaCodec_getOutputFormat(g.codec);
            if (nf) { of = readOutFmt(nf, srcW, srcH); AMediaFormat_delete(nf); }
        } else if (oi == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            if (inputDone && ++idleLoops > 500) break;         // ~1 s sin salida tras el EOS: no esperar eternamente
        }
    }
    if (delivered == 0) { if (err.empty()) err = "Android: el decodificador no devolvio ningun frame"; return false; }
    return true;
}

} // namespace vid
#endif // __ANDROID__
