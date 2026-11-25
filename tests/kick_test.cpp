// Prueba del ANTI-KICK (GD 2.2081: PlayLayer::setupHasCompleted -> shouldExitHackedLevel). Frames sinteticos densos apilados en el origen:
//   - sin anti-kick la celda mas llena (100x100 u, indice = trunc(pos*0.01f)) pasa del limite exigido;
//   - con anti-kick (Toggle / Spawn / Remap, con y sin anti-lag pedido) la CADENA FINAL, medida otra vez aqui de forma independiente, queda por debajo
//     y el simulador de GD sigue viendo cada frame en su instante (la animacion no cambia).
#include "../src/video/videosource.hpp"
#include "../src/engine/videoanim.hpp"
#include "gdsim.hpp"
#include <cstdio>
#include <unordered_map>
using namespace gdx;
static Image makeFrame(int k, int w = 96, int h = 54) {
    Image im; im.w = w; im.h = h; im.rgba.assign(size_t(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { uint8_t* p = im.at(x, y); const int v = ((x / 2 + y / 2 + k) % 2) ? 230 : 25; p[0] = uint8_t(v); p[1] = uint8_t((k * 37 + x) & 255); p[2] = uint8_t(255 - v); }
    return im;
}
// maximo de objetos por celda de la cadena final (independiente de la estimacion interna: parsea las posiciones de TODOS los objetos)
static long maxCellOf(const std::string& chain, double* ratioOut = nullptr) {
    std::vector<float> xs, ys; vdetail::parseXY(chain, xs, ys);
    std::unordered_map<unsigned long long, int> c;
    for (size_t i = 0; i < xs.size(); ++i) ++c[(static_cast<unsigned long long>(vdetail::secIdx(xs[i])) << 32) | static_cast<unsigned>(vdetail::secIdx(ys[i]))];
    long mx = 0; for (auto& kv : c) mx = std::max<long>(mx, kv.second);
    (void)ratioOut; return mx;
}
int main() {
    int bad = 0;
    const int fps = 12, N = 480;                                     // 40 s
    VideoClip clip; clip.fps = fps; clip.length = double(N) / fps;
    for (int i = 0; i < N; ++i) clip.frames.push_back(makeFrame(i));
    VideoPlanOptions po; po.maxTotal = 400000; po.dedupe = false; po.stabilizeTol = 0;
    VideoPlan plan = planVideo(clip, po);
    Options o; o.quality = 0; o.maxRaster = 96;
    auto res = convertVideo(clip, plan, Method::BSP, o, nullptr, 1);
    size_t perFrame = res.empty() ? 0 : res[0].shapes.size();
    printf("frames=%d, ~%zu objetos por frame, %zu en total\n", N, perFrame, perFrame * size_t(N));
    for (int mode = 0; mode < 3; ++mode)
    for (int lagReq = 0; lagReq < 2; ++lagReq)
    for (double fm : {300000.0, 50000.0}) {                                  // farMax pequeno: sin proteccion las partes se pisarian (paso < ancho de la imagen)
        VideoExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.mode = PlayMode(mode);
        p.lag.enable = lagReq; p.lag.chunkObjects = 25000; p.lag.farMax = fm;
        std::string err;
        p.antiKick = false; LagInfo l0; const std::string c0 = buildVideoString(res, plan, p, &err, &l0);
        p.antiKick = true;  LagInfo l1; const std::string c1 = buildVideoString(res, plan, p, &err, &l1);
        if (c0.empty() || c1.empty()) { printf("FAIL build %s\n", err.c_str()); return 1; }
        const long m0 = maxCellOf(c0), m1 = maxCellOf(c1);
        // simulador: cada frame visible en su instante con anti-kick
        auto objs = sim::parse(c1); sim::Sim Sm; Sm.init(c1); sim::Opt so; std::mt19937 rng(1);
        int wrong = 0;
        for (int k = 0; k < N; k += 7) {
            Sm.run(double(k) / fps + 1e-4, so, rng);
            auto it = Sm.opacity.find(100 + plan.group[size_t(k)]);
            if (it != Sm.opacity.end() && it->second == 0) ++wrong;
        }
        const bool okMax = (m1 <= p.kickSafeCell + 12) && l1.kickOk && std::labs(m1 - l1.kickMaxCell) <= 40;   // la estimacion interna coincide con la medida
        const bool improved = m1 <= m0;
        printf("%-6s anti-lag pedido=%d farMax=%.0f: sin anti-kick celda max=%ld | con anti-kick=%ld (estimada %ld, limite GD %d, exigido %d, partes de %d obj, %d partes) %s | frames mal: %d\n",
               mode == 2 ? "Remap" : mode ? "Spawn" : "Toggle", lagReq, fm, m0, m1, l1.kickMaxCell, l1.kickLimit, l1.kickSafe, l1.kickChunkObjects, l1.chunks,
               okMax && improved ? "OK" : "FALLO", wrong);
        if (!okMax || !improved || wrong) ++bad;
        if (m0 <= p.kickSafeCell) { printf("   (aviso: el caso de prueba no supera el limite sin anti-kick; sube N)\n"); }
    }
    printf(bad ? "FALLO\n" : "OK\n");
    return bad ? 1 : 0;
}
