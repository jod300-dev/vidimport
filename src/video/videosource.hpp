// ============================================================================
//  vid :: obtener FRAMES de un MP4 a 12 o 30 fps (o los que pidas), ya reducidos y girados.
//  Sin dependencias de Geode.
//
//  Arquitectura (como el motor: una sola ruta para todo, solo cambia el "backend"):
//
//      backend nativo de la plataforma  --\                        +--> nearest-frame a t = start + k / fps
//        Windows : Media Foundation        \                       |    (como el filtro `fps` de ffmpeg)
//        Android : AMediaCodec (NDK)         >-- RawSink(pixeles)--+--> reduccion rapida + resizeArea
//        macOS/iOS: AVFoundation            /                      |--> rotacion de la matriz del track
//      ffmpeg por linea de comandos        /  (respaldo en PC)     +--> gdx::Image RGBA (almacen del plan)
//
//  Cada backend solo sabe entregar los frames decodificados EN ORDEN con su marca de tiempo (pts, en segundos).
//  Todo lo demas (elegir que frames valen para 12 / 30 fps, escalar, girar) es comun y se prueba en cualquier PC.
// ============================================================================
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../engine/core.hpp"
#include "mp4probe.hpp"

namespace vid {

enum class PixFmt { RGBA, BGRA };

// Un frame decodificado: filas de arriba a abajo, `stride` en BYTES. pts en segundos (desde el inicio del video).
// Devuelve false para parar la decodificacion (cancelar o ya no hacen falta mas frames).
using RawSink = std::function<bool(const uint8_t* px, int w, int h, int stride, PixFmt fmt, double pts)>;

struct DecodeRequest {
    std::string path;
    double start = 0;            // segundo desde el que se necesitan frames
    double end = 0;              // hasta que segundo (los backends pueden pasarse un poco; el muestreador ignora lo que sobra)
    double fps = 12;             // fps pedidos (solo lo usa el backend de ffmpeg para filtrar con `-vf fps`)
    int hintW = 0, hintH = 0;    // tamano de salida PREFERIDO (sin girar); los backends que no escalan lo ignoran
    std::string ffmpegPath;      // respaldo en PC: ruta de ffmpeg ("" = buscar en el PATH)
    bool allowFfmpeg = true;
};

// ---- backends (cada uno se define en su .cpp; los que no existen en la plataforma devuelven false) ----
// Nativo de la plataforma. false + err = no disponible / el sistema no sabe decodificar este video.
bool decodeNative(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel);
// ffmpeg por linea de comandos (solo PC). El sink recibe pts = start + k / fps (ya filtrado por `-vf fps`).
bool decodeFfmpegCli(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel, bool* cliMissing = nullptr);

// ------------------------------------------------------------------ utilidades de imagen ----

// Reduccion rapida por bloques enteros (media de k x k) para videos grandes: deja la imagen a <= 2x del tamano final.
inline gdx::Image shrinkBlocks(const uint8_t* px, int w, int h, int stride, PixFmt fmt, int k) {
    gdx::Image o; o.w = std::max(1, w / k); o.h = std::max(1, h / k);
    o.rgba.resize(size_t(o.w) * o.h * 4);
    const int ri = fmt == PixFmt::BGRA ? 2 : 0, bi = fmt == PixFmt::BGRA ? 0 : 2;
    const int area = k * k;
    for (int y = 0; y < o.h; ++y) {
        uint8_t* d = o.at(0, y);
        for (int x = 0; x < o.w; ++x, d += 4) {
            uint32_t r = 0, g = 0, b = 0;
            for (int j = 0; j < k; ++j) {
                const uint8_t* s = px + size_t(y * k + j) * stride + size_t(x) * k * 4;
                for (int i = 0; i < k; ++i, s += 4) { r += s[ri]; g += s[1]; b += s[bi]; }
            }
            d[0] = uint8_t((r + area / 2) / area); d[1] = uint8_t((g + area / 2) / area); d[2] = uint8_t((b + area / 2) / area); d[3] = 255;
        }
    }
    return o;
}

inline gdx::Image toImage(const uint8_t* px, int w, int h, int stride, PixFmt fmt) {
    gdx::Image o; o.w = w; o.h = h; o.rgba.resize(size_t(w) * h * 4);
    const int ri = fmt == PixFmt::BGRA ? 2 : 0, bi = fmt == PixFmt::BGRA ? 0 : 2;
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = px + size_t(y) * stride; uint8_t* d = o.at(0, y);
        for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[ri]; d[1] = s[1]; d[2] = s[bi]; d[3] = 255; }
    }
    return o;
}

// Gira en sentido horario 0/90/180/270 grados (la rotacion que hay que APLICAR para ver el video derecho).
inline gdx::Image rotateCw(const gdx::Image& s, int deg) {
    if (deg == 0) return s;
    gdx::Image o;
    if (deg == 180) { o.w = s.w; o.h = s.h; } else { o.w = s.h; o.h = s.w; }
    o.rgba.resize(size_t(o.w) * o.h * 4);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            int dx, dy;
            if (deg == 90) { dx = s.h - 1 - y; dy = x; }
            else if (deg == 180) { dx = s.w - 1 - x; dy = s.h - 1 - y; }
            else { dx = y; dy = s.w - 1 - x; }                       // 270
            const uint8_t* p = s.at(x, y); uint8_t* q = o.at(dx, dy);
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3];
        }
    return o;
}

// ------------------------------------------------------------------ muestreador ----

struct SampleSpec {
    std::string path;
    double start = 0;            // s
    double length = 0;           // s (0 = hasta el final)
    double fps = 12;             // 12 o 30
    int storeLong = 480;         // lado largo (px) con el que se GUARDAN los frames
    std::string ffmpegPath;
    bool allowFfmpeg = true;
};

struct SampleResult {
    std::vector<gdx::Image> frames;   // exactamente round(length * fps) frames (los que existan)
    int srcW = 0, srcH = 0;           // tamano del video al reproducirlo
    double fps = 0;
    double start = 0, length = 0;
    std::string backend;              // "Media Foundation", "AMediaCodec", "AVFoundation" o "ffmpeg"
    std::string error;
    bool ok() const { return error.empty() && !frames.empty(); }
};

// Cuantos frames salen de `length` segundos a `fps`: el entero MAS CERCANO (3.0 s a 12 fps = 36 exactos; un tramo de 10.03 s a 12 fps = 120).
// La duracion del bucle no depende de este redondeo: se usa la duracion real del tramo (VideoClip::length), asi el ultimo frame dura lo que reste.
inline int framesFor(double length, double fps) { return std::max(0, int(std::floor(length * fps + 0.5))); }

// Elige, para cada instante t_k = start + k / fps, el frame decodificado MAS CERCANO en el tiempo (igual que `-vf fps` de ffmpeg,
// round=near). Se alimenta con frames en orden de pts; guarda solo los elegidos (el resto ni se convierte).
class Sampler {
public:
    Sampler(double start, double fps, int count, int storeW, int storeH, int rotation)
        : start_(start), fps_(fps), count_(count), sw_(storeW), sh_(storeH), rot_(rotation) {
        out_.reserve(size_t(count));
    }
    // Devuelve false cuando ya estan todos los frames pedidos.
    // Solo se CONVIERTE (reduce) un frame si puede ser el mas cercano a algun instante pendiente: a 30 fps de un video de 60 fps
    // la mitad de los frames se descartan sin tocarlos (la reduccion es lo caro).
    bool feed(const uint8_t* px, int w, int h, int stride, PixFmt fmt, double pts) {
        if (done()) return false;
        const double dur = haveRaw_ ? std::max(1e-4, pts - rawPts_) : 1.0 / fps_;
        if (haveRaw_ && !prev_.empty()) {
            const double mid = 0.5 * (rawPts_ + pts);
            while (!done() && time(next_) < mid) take(prev_);       // t_k mas cerca del frame anterior que de este
        }
        if (done()) return false;
        const bool needed = prev_.empty() || time(next_) <= pts + 0.5 * dur + 1e-6;
        if (needed) prev_ = convert(px, w, h, stride, fmt);
        rawPts_ = pts; haveRaw_ = true;
        return true;
    }
    // Fin del video: los instantes que quedan usan el ultimo frame.
    void finish() { if (!prev_.empty()) while (!done()) take(prev_); }
    bool done() const { return next_ >= count_; }
    int count() const { return next_; }
    std::vector<gdx::Image> take() { return std::move(out_); }
    int outW() const { return ow_; }
    int outH() const { return oh_; }

private:
    double time(int k) const { return start_ + double(k) / fps_; }
    void take(const gdx::Image& im) { out_.push_back(im); ++next_; }
    gdx::Image convert(const uint8_t* px, int w, int h, int stride, PixFmt fmt) {
        // reducir hasta (storeW, storeH) ANTES de girar (si el track esta girado 90/270 el almacen va intercambiado)
        const bool swap = rot_ == 90 || rot_ == 270;
        const int tw = swap ? sh_ : sw_, th = swap ? sw_ : sh_;
        gdx::Image im;
        int k = std::min(w / std::max(1, tw), h / std::max(1, th));
        if (k >= 3) k = std::max(1, k / 2);                                  // deja el frame a ~2x del destino y termina con promedio de area exacto
        else k = 1;
        if (k > 1) im = shrinkBlocks(px, w, h, stride, fmt, k); else im = toImage(px, w, h, stride, fmt);
        if (im.w != tw || im.h != th) im = gdx::resizeArea(im, tw, th);
        if (rot_) im = rotateCw(im, rot_);
        ow_ = im.w; oh_ = im.h;
        return im;
    }
    double start_, fps_; int count_, sw_, sh_, rot_;
    int next_ = 0; bool haveRaw_ = false; double rawPts_ = 0; gdx::Image prev_;
    std::vector<gdx::Image> out_;
    int ow_ = 0, oh_ = 0;
};

// Decodifica el tramo [start, start+length) a `fps` y devuelve los frames. `progress` 0..1 (opcional).
inline SampleResult sampleVideo(const SampleSpec& sp, const Info& info, std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr) {
    SampleResult r;
    r.fps = sp.fps; r.srcW = info.displayW; r.srcH = info.displayH;
    double len = sp.length > 0 ? sp.length : info.duration - sp.start;
    len = std::min(len, info.duration - sp.start);
    r.start = sp.start; r.length = len;
    const int count = framesFor(len, sp.fps);
    if (count < 1) { r.error = "El tramo elegido es demasiado corto"; return r; }

    int fw, fh; gdx::fitSize(info.displayW, info.displayH, sp.storeLong, fw, fh);
    const bool swap = info.rotation == 90 || info.rotation == 270;
    Sampler S(sp.start, sp.fps, count, fw, fh, info.rotation);

    DecodeRequest rq;
    rq.path = sp.path; rq.start = sp.start; rq.end = sp.start + len + 1.0 / sp.fps; rq.fps = sp.fps;
    rq.hintW = swap ? fh : fw; rq.hintH = swap ? fw : fh;
    rq.ffmpegPath = sp.ffmpegPath; rq.allowFfmpeg = sp.allowFfmpeg;

    int seen = 0;
    auto sink = [&](const uint8_t* px, int w, int h, int stride, PixFmt fmt, double pts) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        if (pts < sp.start - 1.0 / sp.fps) {            // anterior al tramo (el backend busco un keyframe anterior): solo el ultimo previo importa
            if (pts < sp.start - 2.0) return true;       // muy lejos: ni se convierte
        }
        const bool more = S.feed(px, w, h, stride, fmt, pts);
        if (progress) progress->store(std::min(1.f, float(S.count()) / float(count)), std::memory_order_relaxed);
        ++seen;
        return more;
    };

    std::string errNative, errCli;
    bool ok = decodeNative(rq, sink, errNative, cancel);
    r.backend = "nativo";
    if (!ok && !(cancel && cancel->load())) {
        if (sp.allowFfmpeg && S.count() == 0 && seen == 0) {
            bool missing = false;
            S = Sampler(sp.start, sp.fps, count, fw, fh, info.rotation);
            ok = decodeFfmpegCli(rq, sink, errCli, cancel, &missing);
            r.backend = "ffmpeg";
            if (!ok) r.error = errNative + (errNative.empty() ? "" : " | ") + errCli;
        } else r.error = errNative;
    }
    if (cancel && cancel->load()) { r.error = "Cancelado"; return r; }
    if (!ok && !r.error.empty()) return r;
    S.finish();
    r.frames = S.take();
    if (r.frames.empty()) r.error = "No se pudo decodificar ningun frame";
    return r;
}

} // namespace vid
