// ============================================================================
//  gdx :: video -> plan de objetos
//  Sin dependencias de Geode (se prueba en cualquier PC).
//
//  Entrada: los frames YA muestreados a 12 o 30 fps (src/video/videosource.hpp).
//  Aqui se decide, con un presupuesto TOTAL de objetos (p. ej. 450000):
//    1. ESTABILIZAR en el tiempo: el ruido de compresion del MP4 hace que el BSP corte distinto en cada frame y la imagen "hierve".
//       Un pixel que casi no cambia respecto al frame anterior conserva su valor -> las zonas quietas dan SIEMPRE las mismas formas.
//    2. DUPLICADOS: un frame casi igual (>= 40 dB) al representante del grupo anterior no crea objetos nuevos: reutiliza el grupo
//       (0 objetos, 0 triggers). Videos con partes quietas, pausas, bucles... ahorran presupuesto que se reparte entre el resto.
//    3. REPARTO ADAPTATIVO: cada grupo recibe objetos segun su detalle (raiz de la varianza por bloques): un frame liso no gasta
//       lo mismo que uno lleno de textura, y la calidad (PSNR) queda mas pareja que con un reparto igual para todos.
//    4. CONVERTIR en paralelo con el motor normal (BSP, Hibrido, ...), un Result por grupo.
// ============================================================================
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>
#include "engine.hpp"

namespace gdx {

struct VideoClip {
    std::vector<Image> frames;      // un frame por instante t_k = k / fps (RGBA opaco)
    int fps = 12;
    double length = 0;              // duracion REAL del tramo en segundos (0 = frames / fps). Con ella el bucle dura exactamente lo que el MP4
};

struct VideoPlanOptions {
    int maxTotal = 1000000;          // tope de objetos totales (triggers incluidos)
    int minPerFrame = 150;          // objetos minimos por grupo: por debajo el frame sale irreconocible
    bool dedupe = true;
    double dedupePsnr = 40.0;       // dB
    bool adaptive = true;
    double adaptiveSpread = 2.5;    // un grupo recibe entre 1/spread y spread veces la media
    int stabilizeTol = 5;           // 0 = sin estabilizar; dif. maxima (0-255) que se considera ruido
    int extraObjects = 0;           // objetos fijos ajenos a los frames (los Color triggers de la paleta en modo canales)
    bool lagMoves = false;          // partes anti-lag: 2 Move triggers por grupo de frames (en modo Remap: 4 triggers por PARTE)
    bool remap = false;             // modo Remap: ~1 trigger por paso (la plantilla de 4 Toggle se reutiliza con remap)
    int lagChunkObjects = 25000;    // objetos por parte anti-lag (para acotar cuantas partes habra en modo Remap)
};

struct VideoPlan {
    int fps = 12;
    int frames = 0;                 // N frames del tramo
    double length = 0;              // duracion de la vuelta en segundos (la del tramo de video; el ultimo frame dura lo que reste)
    std::vector<int> group;         // frame -> grupo (0..U-1, en orden de primera aparicion)
    std::vector<int> rep;           // grupo -> frame representante
    std::vector<int> budget;        // grupo -> objetos
    int events = 0;                 // cambios de grupo en el bucle (cada uno = un paso de triggers)
    long triggerEstimate = 0;       // cota de triggers
    long objectsPlanned = 0;
    int dupFrames = 0;              // frames que reutilizan un grupo
    bool fits = true;               // false: no cabe ni con minPerFrame (hay que acortar el tramo)
    int maxFramesFit = 0;           // frames que SI caben con minPerFrame
    double avgPerGroup = 0;
    int groups() const { return int(rep.size()); }
    // Duracion de la vuelta del bucle. Es la del tramo de video (no N / fps): asi a 12 y a 30 fps la animacion dura EXACTAMENTE lo mismo que el MP4.
    double seconds() const { return length > 0 ? length : (fps > 0 ? double(frames) / fps : 0); }
};

// Cota de triggers de una animacion con `events` pasos y `groups` grupos de frames (vale para Toggle y Spawn).
inline long videoTriggerBound(int events, int groups) { return long(events) * 6 + long(groups) + 16; }
// Cota en modo Remap: 1 Spawn(plantilla) por paso + I0 en lotes de 3 + plantilla y arranque; con anti-lag, 4 triggers por parte
// (como mucho maxTotal / objetos_por_parte + 2 partes).
inline long videoTriggerBoundRemap(int events, int groups, long lagChunks = 0) { return long(events) + long(groups) / 3 + 16 + 4L * lagChunks; }

namespace vdetail {

inline double framePsnrSub(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h) return 0;
    double err = 0; size_t n = 0;
    for (int y = 0; y < a.h; y += 2) {
        const uint8_t* p = a.at(0, y); const uint8_t* q = b.at(0, y);
        for (int x = 0; x < a.w; x += 2, p += 8, q += 8) {
            const double d0 = double(p[0]) - q[0], d1 = double(p[1]) - q[1], d2 = double(p[2]) - q[2];
            err += d0 * d0 + d1 * d1 + d2 * d2; n += 3;
        }
    }
    if (n == 0) return 99;
    const double mse = err / double(n);
    return mse < 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Detalle de un frame: media de la desviacion tipica de bloques 8x8 (sobre el brillo). Mide cuanto cuesta representarlo.
inline double frameDetail(const Image& im) {
    double acc = 0; int cnt = 0;
    for (int by = 0; by + 8 <= im.h; by += 8)
        for (int bx = 0; bx + 8 <= im.w; bx += 8) {
            double s = 0, s2 = 0;
            for (int y = 0; y < 8; ++y) {
                const uint8_t* p = im.at(bx, by + y);
                for (int x = 0; x < 8; ++x, p += 4) { const double l = 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; s += l; s2 += l * l; }
            }
            const double m = s / 64.0, v = std::max(0.0, s2 / 64.0 - m * m);
            acc += std::sqrt(v); ++cnt;
        }
    return cnt ? acc / cnt : 0.0;
}
} // namespace vdetail

// Estabilizacion temporal (en el sitio). Un pixel cuyo valor difiere <= tol del valor ya FILTRADO del frame anterior lo conserva.
// El error maximo introducido es `tol` por canal (invisible); un corte de escena (todo cambia) pasa tal cual.
inline void stabilizeFrames(std::vector<Image>& f, int tol) {
    if (tol <= 0 || f.size() < 2) return;
    for (size_t i = 1; i < f.size(); ++i) {
        const Image& prev = f[i - 1];
        Image& cur = f[i];
        if (prev.w != cur.w || prev.h != cur.h) continue;
        const size_t n = cur.rgba.size();
        for (size_t k = 0; k + 3 < n; k += 4) {
            const int d0 = int(cur.rgba[k]) - prev.rgba[k], d1 = int(cur.rgba[k + 1]) - prev.rgba[k + 1], d2 = int(cur.rgba[k + 2]) - prev.rgba[k + 2];
            if (std::abs(d0) <= tol && std::abs(d1) <= tol && std::abs(d2) <= tol) { cur.rgba[k] = prev.rgba[k]; cur.rgba[k + 1] = prev.rgba[k + 1]; cur.rgba[k + 2] = prev.rgba[k + 2]; }
        }
    }
}

inline VideoPlan planVideo(const VideoClip& clip, const VideoPlanOptions& o) {
    VideoPlan p;
    p.fps = clip.fps; p.frames = int(clip.frames.size());
    const int N = p.frames;
    {   // duracion real del tramo: solo se acepta si cuadra con el numero de frames (a lo sumo medio frame de diferencia)
        const double nominal = clip.fps > 0 ? double(N) / clip.fps : 0.0;
        const double half = clip.fps > 0 ? 0.5 / clip.fps : 0.0;
        p.length = (clip.length > 0 && std::abs(clip.length - nominal) <= half + 1e-6) ? clip.length : nominal;
    }
    p.group.assign(size_t(N), 0);
    if (N == 0) { p.fits = false; return p; }

    // --- grupos: cada frame que no se parece a su representante vigente abre un grupo nuevo
    int cur = -1;
    for (int i = 0; i < N; ++i) {
        const bool same = o.dedupe && cur >= 0 && vdetail::framePsnrSub(clip.frames[size_t(p.rep[size_t(cur)])], clip.frames[size_t(i)]) >= o.dedupePsnr;
        if (!same) { p.rep.push_back(i); cur = int(p.rep.size()) - 1; } else ++p.dupFrames;
        p.group[size_t(i)] = cur;
    }
    const int U = p.groups();
    for (int i = 0; i < N; ++i) if (i == 0 || p.group[size_t(i)] != p.group[size_t(i - 1)]) ++p.events;
    if (U <= 1) p.events = 1;
    if (U <= 1) p.triggerEstimate = 0;
    else if (o.remap) p.triggerEstimate = videoTriggerBoundRemap(p.events, U, o.lagMoves ? long(o.maxTotal / std::max(1, o.lagChunkObjects)) + 2 : 0L);
    else p.triggerEstimate = videoTriggerBound(p.events, U) + (o.lagMoves ? 2L * U : 0L);

    // --- presupuesto
    const long room = std::max(0L, long(o.maxTotal) - p.triggerEstimate - long(o.extraObjects));
    p.avgPerGroup = double(room) / double(std::max(1, U));
    p.maxFramesFit = int(std::min<long>(N, std::max(1L, room / std::max(1, o.minPerFrame))));
    p.fits = p.avgPerGroup >= o.minPerFrame;

    std::vector<double> w(size_t(U), 1.0);
    if (o.adaptive && U > 1) {
        double mean = 0;
        for (int g = 0; g < U; ++g) { w[size_t(g)] = std::sqrt(vdetail::frameDetail(clip.frames[size_t(p.rep[size_t(g)])]) + 1.0); mean += w[size_t(g)]; }
        mean /= U;
        for (auto& x : w) x = std::clamp(x / mean, 1.0 / o.adaptiveSpread, o.adaptiveSpread);
    }
    double sw = 0; for (double x : w) sw += x;
    p.budget.assign(size_t(U), 1);
    long used = 0;
    for (int g = 0; g < U; ++g) {
        int b = int(std::floor(double(room) * w[size_t(g)] / sw));
        b = std::clamp(b, 1, 80000);
        p.budget[size_t(g)] = b; used += b;
    }
    p.objectsPlanned = used;
    return p;
}

// Lado del raster de trabajo segun los objetos de un grupo: ~4 pixeles por objeto como minimo (un BSP de 20000 objetos no cabe en 160x90)
inline int rasterFor(int budget, double aspect, int storeLong, int minLong = 160) {
    const double px = std::max(24000.0, double(budget) * 5.0);
    const double longSide = std::sqrt(px * std::max(aspect, 1.0 / aspect));
    return std::clamp(int(longSide), minLong, std::max(minLong, storeLong));
}

// Convierte cada GRUPO (su frame representante) con el motor normal, en paralelo. Un Result por grupo.
inline std::vector<Result> convertVideo(const VideoClip& clip, const VideoPlan& plan, Method method, const Options& base,
                                        Progress* prog, int threads) {
    const int U = plan.groups();
    std::vector<Result> outv(static_cast<size_t>(U));
    std::atomic<int> next{0}, done{0};
    threads = std::clamp(threads, 1, std::max(1, U));
    auto worker = [&]() {
        for (;;) {
            if (isCancelled(prog)) return;
            const int g = next.fetch_add(1);
            if (g >= U) return;
            try {
                const Image& im = clip.frames[size_t(plan.rep[size_t(g)])];
                Options o = base;
                o.budget = plan.budget[size_t(g)];
                o.seed = base.seed + uint32_t(g) * 7919u;
                const int storeLong = std::max(im.w, im.h);
                o.maxRaster = std::min(std::clamp(base.maxRaster, 64, 2048), rasterFor(o.budget, double(im.w) / std::max(1, im.h), storeLong));
                outv[size_t(g)] = convert(im, method, o, nullptr);
            } catch (...) {
                outv[size_t(g)] = Result{};
            }
            report(prog, float(done.fetch_add(1) + 1) / float(U));
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    return outv;
}

} // namespace gdx
