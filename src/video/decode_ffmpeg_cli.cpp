// Respaldo en PC: si el sistema no sabe decodificar el video (p. ej. Windows "N" sin Media Feature Pack, o un codec raro) y el usuario
// tiene ffmpeg instalado, se le pide que escupa los frames YA filtrados a N fps y reducidos, en bruto (RGBA) por una tuberia.
// No existe en Android / iOS (no se pueden lanzar procesos): ahi decodeFfmpegCli devuelve false.
#include "videosource.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(_WIN32)
  #define VID_HAS_CLI 1
  #include <windows.h>
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_OSX
    #define VID_HAS_CLI 1
  #endif
#elif !defined(__ANDROID__)
  #define VID_HAS_CLI 1          // Linux: solo para las pruebas de escritorio
#endif

namespace vid {

#ifdef VID_HAS_CLI
namespace {

std::string quoteArg(const std::string& s) {
#ifdef _WIN32
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += "\\\""; else o += c; }
    return o + "\"";
#else
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}

#ifdef _WIN32
std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}
#endif

FILE* openPipe(const std::string& cmd) {
#ifdef _WIN32
    // cmd.exe quita el primer y el ultimo " de la linea: se envuelve todo en un par extra
    return _wpopen(widen("\"" + cmd + "\"").c_str(), L"rb");
#else
    return popen(cmd.c_str(), "r");
#endif
}
void closePipe(FILE* f) {
#ifdef _WIN32
    _pclose(f);
#else
    pclose(f);
#endif
}

#ifdef _WIN32
const char* kNull = "2>nul";
#else
const char* kNull = "2>/dev/null";
#endif

bool works(const std::string& exe) {
    FILE* f = openPipe(quoteArg(exe) + " -version " + kNull);
    if (!f) return false;
    char buf[64]; size_t n = std::fread(buf, 1, sizeof buf, f);
    closePipe(f);
    return n >= 6 && std::strncmp(buf, "ffmpeg", 6) == 0;
}

std::string findFfmpeg(const std::string& user) {
    std::vector<std::string> cand;
    if (!user.empty()) cand.push_back(user);
    cand.push_back("ffmpeg");
#ifdef __APPLE__
    cand.push_back("/opt/homebrew/bin/ffmpeg"); cand.push_back("/usr/local/bin/ffmpeg");
#endif
    for (auto& c : cand) if (works(c)) return c;
    return std::string();
}
} // namespace

bool decodeFfmpegCli(const DecodeRequest& rq, const RawSink& sink, std::string& err, std::atomic<bool>* cancel, bool* cliMissing) {
    if (cliMissing) *cliMissing = false;
    if (rq.hintW <= 0 || rq.hintH <= 0) { err = "ffmpeg: falta el tamano de salida"; return false; }
    const std::string exe = findFfmpeg(rq.ffmpegPath);
    if (exe.empty()) {
        if (cliMissing) *cliMissing = true;
        err = "ffmpeg no esta instalado (ni en el PATH ni en la ruta de Ajustes)";
        return false;
    }
    char num[64];
    std::string cmd = quoteArg(exe) + " -v error -nostdin -noautorotate";
    std::snprintf(num, sizeof num, " -ss %.4f", std::max(0.0, rq.start)); cmd += num;
    cmd += " -i " + quoteArg(rq.path);
    std::snprintf(num, sizeof num, " -t %.4f", std::max(0.05, rq.end - rq.start)); cmd += num;
    std::snprintf(num, sizeof num, "fps=%.6f:round=near", rq.fps);
    cmd += " -an -sn -dn -vf \"" + std::string(num);
    std::snprintf(num, sizeof num, ",scale=%d:%d:flags=area\"", rq.hintW, rq.hintH); cmd += num;
    cmd += " -pix_fmt rgba -f rawvideo - ";
    cmd += kNull;

    FILE* f = openPipe(cmd);
    if (!f) { err = "No se pudo lanzar ffmpeg"; return false; }
    const size_t fs = size_t(rq.hintW) * size_t(rq.hintH) * 4;
    std::vector<uint8_t> buf(fs);
    int k = 0; bool stop = false;
    while (std::fread(buf.data(), 1, fs, f) == fs) {
        if (stop) continue;                                     // seguir leyendo (y tirar) hasta el final: si no, ffmpeg se queda bloqueado en la tuberia
        const double pts = rq.start + double(k) / rq.fps;
        ++k;
        if (!sink(buf.data(), rq.hintW, rq.hintH, rq.hintW * 4, PixFmt::RGBA, pts)) stop = true;
        if (cancel && cancel->load(std::memory_order_relaxed)) stop = true;
    }
    closePipe(f);
    if (k == 0) { err = "ffmpeg no produjo ningun frame (formato no soportado?)"; return false; }
    return true;
}

#else  // sin procesos (Android / iOS)

bool decodeFfmpegCli(const DecodeRequest&, const RawSink&, std::string& err, std::atomic<bool>*, bool* cliMissing) {
    if (cliMissing) *cliMissing = true;
    err = "ffmpeg no se puede usar en esta plataforma";
    return false;
}

#endif

} // namespace vid
