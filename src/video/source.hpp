// ============================================================================
//  vx :: lectura de video. Cada "origen" entrega los frames ya muestreados a la tasa pedida (12 o 30 fps) y reducidos al tamano de trabajo.
//    * Windows  : Media Foundation (nativo, sin instalar nada)            -> source_wmf.cpp
//    * Escritorio (Windows/macOS/Linux): ffmpeg externo, si esta instalado  (cualquier formato)
//    * Todas las plataformas: GIF animado y carpeta de imagenes (PNG/JPG/BMP numeradas)
//  Android/iOS: NO hay decodificador de MP4 integrado (convierte el video a GIF o a una carpeta de imagenes).
// ============================================================================
#pragma once
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include "core.hpp"
#include "gifdecode.hpp"
#include "palette.hpp"
#include "types.hpp"
#ifdef VX_WITH_STB
#ifndef STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#include "stb_image.h"
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define VX_HAS_PIPE 1
#endif

namespace vx {
namespace fs = std::filesystem;

struct DecodeRequest {
    fs::path path;
    double fps = 30;                 // tasa de salida (12 o 30)
    double startSec = 0;
    double maxSeconds = 10;          // duracion maxima a leer
    int maxSide = 384;               // lado largo de los frames de trabajo
    double seqFps = 30;              // tasa de una carpeta de imagenes
    bool gray = false;
    std::string ffmpegPath;          // vacio = buscar
    Progress* prog = nullptr;
};

struct VideoInfo { double duration = 0, fps = 0; int w = 0, h = 0; bool ok = false; std::string how; };

#ifdef _WIN32
bool decodeWmf(const DecodeRequest& req, Clip& out, std::string& err);       // source_wmf.cpp
bool probeWmf(const fs::path& path, VideoInfo& info);
#endif

namespace src {

inline std::string lower(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }

// ---- reducir al tamano de trabajo --------------------------------------------------------------------------------------------
inline Image reduce(const Image& im, int maxSide, bool gray) {
    int nw, nh; gdx::fitSize(im.w, im.h, maxSide, nw, nh);
    Image r = (nw == im.w && nh == im.h) ? im : gdx::resizeArea(im, nw, nh);
    for (size_t i = 3; i < r.rgba.size(); i += 4) r.rgba[i] = 255;
    return gray ? gdx::toGrayscale(r) : r;
}

// ---- GIF ----------------------------------------------------------------------------------------------------------------------
inline bool decodeGifFile(const DecodeRequest& rq, Clip& out, std::string& err) {
    std::ifstream f(rq.path, std::ios::binary);
    if (!f) { err = "No se pudo abrir el GIF"; return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    gdx::gifdec::Out o;
    if (!gdx::gifdec::decode(bytes.data(), bytes.size(), 300u * 1024u * 1024u, o) || o.frames < 2) { err = "GIF no valido o con un solo frame"; return false; }
    const size_t fs_ = size_t(o.w) * o.h * 4; std::vector<double> start(size_t(o.frames) + 1, 0.0);
    for (int i = 0; i < o.frames; ++i) start[size_t(i) + 1] = start[size_t(i)] + (o.delaysMs[size_t(i)] <= 10 ? 100 : o.delaysMs[size_t(i)]) / 1000.0;
    const double dur = start.back(), dt = 1.0 / rq.fps;
    out.srcDuration = dur; out.srcW = o.srcW; out.srcH = o.srcH; out.srcFps = o.frames / std::max(0.01, dur);
    int j = 0;
    for (double t = rq.startSec; t < dur && t < rq.startSec + rq.maxSeconds - 1e-9; t += dt) {
        while (j + 1 < o.frames && start[size_t(j) + 1] <= t) ++j;
        Image im; im.w = o.w; im.h = o.h; im.rgba.assign(o.rgba.begin() + std::ptrdiff_t(size_t(j) * fs_), o.rgba.begin() + std::ptrdiff_t(size_t(j + 1) * fs_));
        out.frames.push_back(reduce(im, rq.maxSide, rq.gray));
    }
    return true;
}

// ---- carpeta de imagenes ------------------------------------------------------------------------------------------------------
#ifdef VX_WITH_STB
inline bool decodeImageDir(const DecodeRequest& rq, Clip& out, std::string& err) {
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(rq.path)) {
        if (!e.is_regular_file()) continue;
        std::string ext = lower(e.path().extension().string());
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {      // orden natural (frame2 < frame10)
        std::string x = a.filename().string(), y = b.filename().string(); size_t i = 0, j = 0;
        while (i < x.size() && j < y.size()) {
            if (std::isdigit((unsigned char)x[i]) && std::isdigit((unsigned char)y[j])) {
                size_t i2 = i, j2 = j; while (i2 < x.size() && std::isdigit((unsigned char)x[i2])) ++i2; while (j2 < y.size() && std::isdigit((unsigned char)y[j2])) ++j2;
                unsigned long long u = std::stoull(x.substr(i, std::min<size_t>(i2 - i, 18))), v = std::stoull(y.substr(j, std::min<size_t>(j2 - j, 18)));
                if (u != v) return u < v; i = i2; j = j2;
            } else { if (x[i] != y[j]) return x[i] < y[j]; ++i; ++j; }
        }
        return x.size() < y.size();
    });
    if (files.size() < 2) { err = "La carpeta necesita al menos 2 imagenes (png/jpg/bmp)"; return false; }
    const double sfps = std::clamp(rq.seqFps, 1.0, 240.0), dur = files.size() / sfps, dt = 1.0 / rq.fps;
    out.srcFps = sfps; out.srcDuration = dur;
    for (double t = rq.startSec; t < dur && t < rq.startSec + rq.maxSeconds - 1e-9; t += dt) {
        size_t k = std::min(files.size() - 1, size_t(t * sfps));
        std::ifstream f(files[k], std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w, h, c; unsigned char* px = stbi_load_from_memory(b.data(), int(b.size()), &w, &h, &c, 4);
        if (!px) continue;
        Image im; im.w = w; im.h = h; im.rgba.assign(px, px + size_t(w) * h * 4); stbi_image_free(px);
        if (out.srcW == 0) { out.srcW = w; out.srcH = h; }
        out.frames.push_back(reduce(im, rq.maxSide, rq.gray));
        gdx::report(rq.prog, float(t - rq.startSec) / float(std::max(0.1, std::min(rq.maxSeconds, dur - rq.startSec))));
    }
    return true;
}
#endif

// ---- ffmpeg externo (escritorio) ------------------------------------------------------------------------------------------------
#ifdef VX_HAS_PIPE
#ifdef _WIN32
using Str = std::wstring; inline Str W(const char* s) { return Str(s, s + std::strlen(s)); } inline FILE* popenRb(const Str& c) { return _wpopen(c.c_str(), L"rb"); } inline void pclose_(FILE* f) { _pclose(f); }
inline Str quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }
inline Str nullErr() { return L" 2>nul"; }
inline Str wrap(const Str& c) { return L"\"" + c + L"\""; }       // cmd /c exige un par de comillas extra alrededor de todo
#else
using Str = std::string; inline Str W(const char* s) { return Str(s); } inline FILE* popenRb(const Str& c) { return popen(c.c_str(), "r"); } inline void pclose_(FILE* f) { pclose(f); }
inline Str quote(const fs::path& p) { std::string s = p.string(), o = "'"; for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; } return o + "'"; }
inline Str nullErr() { return " 2>/dev/null"; }
inline Str wrap(const Str& c) { return c; }
#endif
inline Str numStr(double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return W(b); }

inline bool ffmpegWorks(const fs::path& exe) {
    FILE* f = popenRb(wrap(quote(exe) + W(" -version") + nullErr())); if (!f) return false;
    char buf[64]; size_t n = std::fread(buf, 1, sizeof buf, f); pclose_(f);
    return n >= 6 && std::strncmp(buf, "ffmpeg", 6) == 0;
}
inline fs::path findFfmpeg(const std::string& custom) {
    std::vector<fs::path> c;
    if (!custom.empty()) c.push_back(custom);
#ifdef _WIN32
    c.push_back("ffmpeg.exe"); c.push_back("C:\\ffmpeg\\bin\\ffmpeg.exe"); c.push_back("C:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe");
#else
    c.push_back("ffmpeg"); c.push_back("/opt/homebrew/bin/ffmpeg"); c.push_back("/usr/local/bin/ffmpeg"); c.push_back("/usr/bin/ffmpeg");
#endif
    for (auto& p : c) if (ffmpegWorks(p)) return p;
    return {};
}

inline VideoInfo probeFfmpeg(const fs::path& ffmpeg, const fs::path& file) {
    VideoInfo vi; vi.how = "ffmpeg";
    FILE* f = popenRb(wrap(quote(ffmpeg) + W(" -hide_banner -i ") + quote(file) + W(" 2>&1")));
    if (!f) return vi;
    std::string txt; char buf[4096]; size_t n; while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) txt.append(buf, n);
    pclose_(f);
    std::smatch m;
    if (std::regex_search(txt, m, std::regex(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))"))) vi.duration = std::stod(m[1]) * 3600 + std::stod(m[2]) * 60 + std::stod(m[3]);
    auto vpos = txt.find("Video:");
    if (vpos != std::string::npos) {
        std::string line = txt.substr(vpos, txt.find('\n', vpos) - vpos);
        if (std::regex_search(line, m, std::regex(R"((\d{2,5})x(\d{2,5}))"))) { vi.w = std::stoi(m[1]); vi.h = std::stoi(m[2]); }
        if (std::regex_search(line, m, std::regex(R"((\d+(?:\.\d+)?)\s*fps)"))) vi.fps = std::stod(m[1]);
        vi.ok = vi.w > 0;
    }
    return vi;
}

struct PipeReader {
    FILE* f; std::vector<uint8_t> buf; size_t pos = 0, end = 0;
    explicit PipeReader(FILE* fp) : f(fp), buf(1 << 20) {}
    int get() { if (pos == end) { end = std::fread(buf.data(), 1, buf.size(), f); pos = 0; if (end == 0) return -1; } return buf[pos++]; }
    bool read(uint8_t* d, size_t n) { while (n) { if (pos == end) { end = std::fread(buf.data(), 1, buf.size(), f); pos = 0; if (end == 0) return false; } size_t k = std::min(n, end - pos); std::memcpy(d, &buf[pos], k); pos += k; d += k; n -= k; } return true; }
    bool token(std::string& t) { t.clear(); int c; while ((c = get()) >= 0 && std::isspace(c)) {} if (c < 0) return false; do { t += char(c); } while ((c = get()) >= 0 && !std::isspace(c)); return true; }
};

inline bool decodeFfmpeg(const DecodeRequest& rq, Clip& out, std::string& err) {
    fs::path ff = findFfmpeg(rq.ffmpegPath);
    if (ff.empty()) { err = "No se encontro ffmpeg. Instalalo (ffmpeg.org) y ponlo en el PATH, o indica su ruta en Ajustes > Ruta de ffmpeg"; return false; }
    VideoInfo vi = probeFfmpeg(ff, rq.path); out.srcDuration = vi.duration; out.srcFps = vi.fps; out.srcW = vi.w; out.srcH = vi.h;
    const int M = std::clamp(rq.maxSide, 32, 1024);
    Str cmd = quote(ff) + W(" -v error -ss ") + numStr(rq.startSec) + W(" -t ") + numStr(rq.maxSeconds) + W(" -i ") + quote(rq.path) +
              W(" -vf \"fps=") + numStr(rq.fps) + W(",scale=w=min(") + W(std::to_string(M).c_str()) + W("\\,iw):h=min(") + W(std::to_string(M).c_str()) +
              W("\\,ih):force_original_aspect_ratio=decrease:flags=area\" -an -f image2pipe -vcodec ppm -") + nullErr();
    FILE* f = popenRb(wrap(cmd));
    if (!f) { err = "No se pudo ejecutar ffmpeg"; return false; }
    PipeReader pr(f); std::string t;
    const double expect = std::max(1.0, rq.maxSeconds * rq.fps);
    while (pr.token(t) && t == "P6") {
        std::string sw, sh, mx;
        if (!pr.token(sw) || !pr.token(sh) || !pr.token(mx)) break;
        const int w = std::atoi(sw.c_str()), h = std::atoi(sh.c_str());
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) break;
        std::vector<uint8_t> rgb(size_t(w) * h * 3);
        if (!pr.read(rgb.data(), rgb.size())) break;
        Image im; im.w = w; im.h = h; im.rgba.resize(size_t(w) * h * 4);
        for (size_t i = 0; i < size_t(w) * h; ++i) { im.rgba[i * 4] = rgb[i * 3]; im.rgba[i * 4 + 1] = rgb[i * 3 + 1]; im.rgba[i * 4 + 2] = rgb[i * 3 + 2]; im.rgba[i * 4 + 3] = 255; }
        out.frames.push_back(rq.gray ? gdx::toGrayscale(im) : std::move(im));
        gdx::report(rq.prog, std::min(1.f, float(out.frames.size() / expect)));
        if (gdx::isCancelled(rq.prog)) break;
    }
    pclose_(f);
    if (out.frames.empty()) { err = "ffmpeg no devolvio frames (formato no soportado o ruta incorrecta)"; return false; }
    return true;
}
#endif

} // namespace src

// Punto de entrada unico. Elige el origen segun lo que sea la ruta y la plataforma.
inline bool decodeVideo(const DecodeRequest& rq, Clip& out, std::string& err) {
    out = Clip{}; out.fps = rq.fps; out.name = rq.path.filename().string();
    const std::string ext = src::lower(rq.path.extension().string());
    bool ok = false;
    std::error_code ec;
    if (fs::is_directory(rq.path, ec)) {
#ifdef VX_WITH_STB
        ok = src::decodeImageDir(rq, out, err);
#else
        err = "Carpetas de imagenes no disponibles en esta compilacion";
#endif
    } else if (ext == ".gif") {
        ok = src::decodeGifFile(rq, out, err);
    } else {
        err.clear();
#ifdef _WIN32
        std::string e1;
        ok = decodeWmf(rq, out, e1);
        if (!ok) { out.frames.clear(); }
        if (!ok) { ok = src::decodeFfmpeg(rq, out, err); if (!ok) err = e1 + " | " + err; }
#elif defined(VX_HAS_PIPE)
        ok = src::decodeFfmpeg(rq, out, err);
#else
        err = "Este dispositivo no tiene decodificador de MP4 integrado. Convierte el video a GIF o a una carpeta de imagenes (PNG/JPG numeradas) y elige eso.";
#endif
    }
    if (!ok) return false;
    if (out.frames.size() < 2) { err = "El video tiene menos de 2 frames en el rango pedido"; return false; }
    // todos los frames al mismo tamano (el del primero)
    out.w = out.frames[0].w; out.h = out.frames[0].h;
    for (auto& f : out.frames) if (f.w != out.w || f.h != out.h) f = gdx::resizeArea(f, out.w, out.h);
    return true;
}

inline VideoInfo probeVideo(const fs::path& path, const std::string& ffmpegPath) {
    VideoInfo vi;
#ifdef _WIN32
    if (probeWmf(path, vi)) return vi;
#endif
#ifdef VX_HAS_PIPE
    fs::path ff = src::findFfmpeg(ffmpegPath);
    if (!ff.empty()) return src::probeFfmpeg(ff, path);
#endif
    (void)ffmpegPath; return vi;
}

} // namespace vx
