// Prueba del ANTI-LAG por partes: mismo video con y sin partes en el simulador de GD.
//   - la imagen de cada frame debe salir IGUAL (mismo PSNR contra el video)
//   - objetos cerca de la camara (todos / activos) a lo largo de la reproduccion
//   - Move triggers por vuelta y objetos movidos en un mismo tick
//   - sin deriva: tras varias vueltas las posiciones vuelven a ser las de la misma fase
#include "../src/video/videosource.hpp"
#include "../src/engine/videoanim.hpp"
#include "gdsim.hpp"
#include <cstdio>
#include <cstdlib>
#include <set>
using namespace gdx;
int main(int argc, char** argv) {
    std::string path = argv[1]; int fps = atoi(argv[2]); int mode = atoi(argv[3]); int maxTotal = atoi(argv[4]); int chunk = atoi(argv[5]); double len = atof(argv[6]);
    double lead = argc > 7 ? atof(argv[7]) : 0.5;
    vid::Info in = vid::probe(path);
    vid::SampleSpec sp; sp.path = path; sp.fps = fps; sp.length = len; sp.storeLong = 320;
    auto sr = vid::sampleVideo(sp, in);
    if (!sr.ok()) { printf("ERR %s\n", sr.error.c_str()); return 1; }
    VideoClip clip; clip.fps = fps; clip.frames = sr.frames; std::vector<Image> orig = clip.frames;
    const int CH = getenv("CHAN") ? atoi(getenv("CHAN")) : 0;
    VideoPlanOptions po; po.maxTotal = maxTotal; po.lagMoves = true; po.extraObjects = CH;
    stabilizeFrames(clip.frames, po.stabilizeTol);
    VideoPlan plan = planVideo(clip, po);
    Options o; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.maxRaster = 320;
    auto res = convertVideo(clip, plan, Method::BSP, o, nullptr, 1);
    size_t nobj = 0; for (auto& r : res) nobj += r.shapes.size();
    std::vector<std::array<uint8_t, 3>> pal; if (CH) pal = quantizeVideo(res, CH);
    const double ppu = 0.5; const double asp = double(orig[0].w) / orig[0].h; const double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
    const int W = int(sw * ppu), H = int(sh * ppu);
    std::string chains[2]; LagInfo li[2];
    for (int lag = 0; lag < 2; ++lag) {
        VideoExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.mode = PlayMode(mode);
        p.palette = pal; p.lag.enable = lag == 1; p.lag.chunkObjects = chunk; p.lag.leadSeconds = lead;
        if (getenv("SPREAD")) { p.lag.spread = true; p.lag.farStep = atof(getenv("SPREAD")); }
        std::string err; chains[lag] = buildVideoString(res, plan, p, &err, &li[lag]);
        if (chains[lag].empty()) { printf("FAIL %s\n", err.c_str()); return 1; }
    }
    printf("N=%d groups=%d obj=%zu | anti-lag: %s chunks=%d far=%.0f moves=%ld %s\n", plan.frames, plan.groups(), nobj, li[1].active ? "ACTIVO" : "no aplicado", li[1].chunks, li[1].farDist, li[1].moveTriggers, li[1].note.c_str());
    for (int lag = 0; lag < 2; ++lag) { size_t n = 1; for (char c : chains[lag]) if (c == ';') ++n; printf("  total de objetos de la cadena %s: %zu (tope %d) %s\n", lag ? "CON partes" : "sin partes", n, maxTotal, n <= size_t(maxTotal) ? "OK" : "SE PASA DEL TOPE"); if (n > size_t(maxTotal)) return 3; }
    if (!li[1].active) return 2;
    const double dt = 1.0 / fps, T = plan.seconds();
    int bad = 0;
    for (int variant = 0; variant < 3; ++variant) {
        sim::Opt so; if (variant >= 1) { so.randomTies = true; so.seed = 7; } if (variant == 2) { so.tick = 1.0 / 60; so.reverseTick = true; }
        double avg[2] = {0, 0}, mn[2] = {1e9, 1e9}; long maxNear[2] = {0, 0}, maxActive[2] = {0, 0}, sumActive[2] = {0, 0}; int cnt = 0;
        long maxMoved = 0, moves = 0;
        std::vector<double> x0;                                   // posiciones al inicio de la vuelta 2 y de la vuelta 4 (misma fase): deben coincidir
        double drift = 0;
        for (int lag = 0; lag < 2; ++lag) {
            sim::Sim Sm; Sm.init(chains[lag]); std::mt19937 rng(so.seed);
            std::vector<double> snap[2];
            for (int rep = 0; rep < 4; ++rep) {
                double t = rep * T;
                for (int i = 0; i < plan.frames; ++i) {
                    Sm.run(t + 1e-4, so, rng);
                    if (lag == 1 && variant == 0 && rep == 1 && i == plan.frames / 2) {            // reparto de distancias de las partes que esperan
                        std::set<long> bands; double mn2 = 1e18, mx2 = -1e18;
                        for (auto& ob : Sm.objs) if (ob.id == 3092 && ob.x > 5000) { bands.insert(long(std::lround(ob.x / 1000.0))); mn2 = std::min(mn2, ob.x); mx2 = std::max(mx2, ob.x); }
                        printf("  (a mitad de vuelta) objetos en espera: x de %.0f a %.0f, %zu franjas de 1000 u distintas\n", mn2, mx2, bands.size());
                    }
                    if (i == 0 && (rep == 1 || rep == 3)) { for (auto& ob : Sm.objs) snap[rep == 1 ? 0 : 1].push_back(ob.x); }
                    if (rep >= 1 && (i % 5 == 0)) {
                        long nr, na; Sm.nearCounts(0, 330.0 + 0.5 * 600, nr, na);
                        maxNear[lag] = std::max(maxNear[lag], nr); maxActive[lag] = std::max(maxActive[lag], na); sumActive[lag] += nr; 
                        Image r = Sm.render(0, 0, ppu, W, H, so, rng);
                        Image tgt = resizeArea(orig[size_t(i)], W, H);
                        double ps = computePsnr(tgt, r, 1);
                        if (ps < 15 && variant == 0 && rep >= 1) printf("    [lag=%d rep=%d] frame %d PSNR %.1f\n", lag, rep, i, ps);
                        if (rep == 3) { avg[lag] += ps; mn[lag] = std::min(mn[lag], ps); if (lag == 0) ++cnt; }
                    }
                    t += dt;
                }
            }
            if (lag == 1) {
                moves = Sm.movesTotal; maxMoved = Sm.maxMovedInRound;
                for (size_t k = 0; k < snap[0].size() && k < snap[1].size(); ++k) drift = std::max(drift, std::abs(snap[0][k] - snap[1][k]));
            }
        }
        printf("  variante %d: PSNR sin partes %.1f (min %.1f) | con partes %.1f (min %.1f) | cerca de camara max: %ld -> %ld objetos | Move/4 vueltas=%ld, max objetos movidos en un tick=%ld, deriva entre vueltas=%.1f u\n",
               variant, avg[0] / cnt, mn[0], avg[1] / cnt, mn[1], maxNear[0], maxNear[1], moves, maxMoved, drift);
        if (std::abs(avg[0] - avg[1]) / cnt > 0.3 || drift > 1e-6) ++bad;
    }
    printf(bad ? "FALLO\n" : "OK\n");
    return bad ? 1 : 0;
}
