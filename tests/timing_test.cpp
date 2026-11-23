// Prueba de TIEMPO y de la REJILLA DE TRIGGERS (sin ffmpeg: frames sinteticos).
//   - la duracion del bucle es la del tramo de video (no N/fps) a 12 y a 30 fps: Spawn(M, total) y el ultimo paso caben en `total`
//   - el simulador de GD ve cada frame en su instante exacto (t_k = k/fps * escala) y el bucle se relanza a los `total` s
//   - la rejilla de triggers es densa (paso 30 u), 50 columnas mas ancha que la base, y ninguna celda se repite
#include "../src/video/videosource.hpp"
#include "../src/engine/videoanim.hpp"
#include "gdsim.hpp"
#include <cstdio>
#include <set>
using namespace gdx;
static Image makeFrame(int k, int w = 96, int h = 54) {
    Image im; im.w = w; im.h = h; im.rgba.assign(size_t(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) { uint8_t* p = im.at(x, y); const int v = ((x / 12 + y / 9 + k) % 2) ? 230 : 25; p[0] = uint8_t(v); p[1] = uint8_t((k * 37) & 255); p[2] = uint8_t(255 - v); }
    return im;
}
int main() {
    int bad = 0;
    for (int fps : {12, 30})
    for (double scale : {1.0, 1.05}) {
        const double length = 10.033;                                    // NO multiplo de 1/fps: el bucle debe durar 10.033 s, no N/fps
        const int N = vid::framesFor(length, fps);
        VideoClip clip; clip.fps = fps; clip.length = length;
        for (int i = 0; i < N; ++i) clip.frames.push_back(makeFrame(i));
        VideoPlanOptions po; po.maxTotal = 60000; po.dedupe = false; po.stabilizeTol = 0;
        VideoPlan plan = planVideo(clip, po);
        Options o; o.quality = 0; o.maxRaster = 96;
        auto res = convertVideo(clip, plan, Method::BSP, o, nullptr, 1);
        for (int mode = 0; mode < 3; ++mode) {
            VideoExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.mode = PlayMode(mode); p.timeScale = scale;
            std::string err; const std::string chain = buildVideoString(res, plan, p, &err);
            if (chain.empty()) { printf("FAIL %s\n", err.c_str()); return 1; }
            auto objs = sim::parse(chain);
            // 1) Spawn(M, total) (el que se spawnea a si mismo) debe tener retardo = duracion REAL del tramo * escala
            double loopDelay = -1; std::set<std::pair<long, long>> cells; double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18; long nTrig = 0;
            for (auto& ob : objs) {
                if (ob.id == 1268 && ob.has(62) && std::find(ob.groups.begin(), ob.groups.end(), int(ob.geti(51))) != ob.groups.end()) loopDelay = ob.geti(63);
                if ((ob.id == 1268 || ob.id == 1049 || ob.id == 901) && ob.has(62)) {
                    ++nTrig; minX = std::min(minX, ob.x); maxX = std::max(maxX, ob.x); minY = std::min(minY, ob.y); maxY = std::max(maxY, ob.y);
                    cells.insert({std::lround(ob.x * 100), std::lround(ob.y * 100)});
                }
            }
            const double want = length * scale;
            const bool tOk = std::abs(loopDelay - want) < 2e-4;
            // 2) el simulador: el frame k aparece en k/fps*escala (Toggle absoluto o cadena)
            sim::Sim Sm; Sm.init(chain); sim::Opt so; std::mt19937 rng(1);
            int wrong = 0;
            for (int rep = 0; rep < 2; ++rep)
                for (int k = 0; k < N; ++k) {
                    const double t = rep * want + double(k) / fps * scale;
                    Sm.run(t + 1e-4, so, rng);
                    const int g = 100 + plan.group[size_t(k)];                        // grupo G de ese frame (se asignan en orden de uso)
                    auto it = Sm.opacity.find(g);
                    if (it != Sm.opacity.end() && it->second == 0) ++wrong;           // el frame de este instante debe estar mostrado
                }
            // 3) rejilla: ancho y alto ampliados 50 bloques (1500 u) respecto a la rejilla base de 80 columnas
            const double w = maxX - minX, h = maxY - minY;
            const long cellsN = nTrig;
            const TriggerGrid g0 = planTriggerGrid(cellsN, 80, 0), g1 = planTriggerGrid(cellsN, 80, 50);
            const double baseW = (g0.cols - 1) * 30.0, baseH = (std::min<long>(cellsN, long(g0.cols) * g0.rows) > 0 ? double(g0.rows - 1) * 30.0 : 0);
            const bool uniq = cells.size() == size_t(nTrig);
            printf("fps=%d escala=%.2f %s: bucle=%.4f s (esperado %.4f) %s | frame/instante mal: %d | triggers=%ld ancho=%.0f u (base %.0f) alto=%.0f u (base %.0f; cols %d filas %d pasoY %.1f) %s\n",
                   fps, scale, mode == 2 ? "Remap " : (mode ? "Spawn " : "Toggle"), loopDelay, want, tOk ? "OK" : "FALLO", wrong, nTrig, w, baseW, h, baseH, g1.cols, g1.rows, g1.pitchY, uniq ? "celdas unicas" : "CELDAS REPETIDAS");
            if (!tOk || wrong > 0 || !uniq || w < baseW + 50 * 30.0 - 1.0 || g1.pitchY != 30.0 || g1.pitchX != 30.0) ++bad;
        }
    }
    printf(bad ? "FALLO\n" : "OK\n");
    return bad ? 1 : 0;
}
