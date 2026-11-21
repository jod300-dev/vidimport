// Prueba de punta a punta: MP4 -> frames (ffmpeg backend) -> plan -> objetos -> cadena GD -> simulador -> PSNR contra el video.
#include "../src/video/videosource.hpp"
#include "../src/engine/videoanim.hpp"
#include "gdsim.hpp"
#include <cstdio>
#include <fstream>
using namespace gdx;
int main(int argc, char** argv) {
    std::string path = argv[1]; int fps = atoi(argv[2]); int mode = atoi(argv[3]); int maxTotal = atoi(argv[4]); int method = argc > 5 ? atoi(argv[5]) : 2; double len = argc > 6 ? atof(argv[6]) : 0;
    vid::Info in = vid::probe(path);
    vid::SampleSpec sp; sp.path = path; sp.fps = fps; sp.length = len; sp.storeLong = 384;
    auto sr = vid::sampleVideo(sp, in);
    if (!sr.ok()) { printf("ERR %s\n", sr.error.c_str()); return 1; }
    VideoClip clip; clip.fps = fps; clip.frames = sr.frames;
    std::vector<Image> orig = clip.frames;
    double tDec = nowMs(); printf("decode %.1fs (%zu frames)\n", 0.0, clip.frames.size());
    double t0 = nowMs();
    VideoPlanOptions po; po.maxTotal = maxTotal;
    stabilizeFrames(clip.frames, po.stabilizeTol);
    VideoPlan plan = planVideo(clip, po);
    Options o; o.quality = 0; o.lightSpecialChannels = true; o.maxChannels = 0; o.maxRaster = 384;
    auto res = convertVideo(clip, plan, Method(method), o, nullptr, 1);
    VideoExportParams p; p.baseGroup = 100; p.eo.longSide = 600; p.mode = PlayMode(mode);
    std::string err; std::string chain = buildVideoString(res, plan, p, &err);
    double ms = nowMs() - t0;
    if (chain.empty()) { printf("FAIL %s\n", err.c_str()); return 1; }
    size_t nobj = 0; double ppsnr = 0; for (auto& r : res) { nobj += r.shapes.size(); ppsnr += r.psnr / res.size(); }
    printf("preview PSNR medio (motor) %.1f dB\n", ppsnr);
    auto S = scheduleVideo(plan, p.mode);
    // contar triggers reales en la cadena
    size_t ntr = 0, nTotal = 0; { size_t pos = 0; while (pos < chain.size()) { size_t e = chain.find(';', pos); if (e == std::string::npos) e = chain.size(); std::string it = chain.substr(pos, e - pos); ++nTotal; if (it.rfind("1,1049,", 0) == 0 || it.rfind("1,1268,", 0) == 0) ++ntr; pos = e + 1; } }
    printf("N=%d groups=%d dup=%d events=%d budgetSum=%ld obj=%zu trig(real)=%zu trig(calc)=%ld total=%zu build=%.0fms chain=%zuKB mode=%s\n", plan.frames, plan.groups(), plan.dupFrames, plan.events, plan.objectsPlanned, nobj, ntr, S.triggers, nTotal, ms, chain.size()/1024, mode ? "Spawn" : "Toggle");
    if (getenv("NOSIM")) return 0;
    // simulacion
    const double ppu = 0.6; double asp = double(orig[0].w) / orig[0].h; double sw = asp >= 1 ? 600 : 600 * asp, sh = asp >= 1 ? 600 / asp : 600;
    int W = int(sw * ppu), H = int(sh * ppu);
    const double dt = 1.0 / fps;
    for (int variant = 0; variant < 6; ++variant) {
        sim::Opt so; if (variant >= 1) { so.randomTies = true; so.seed = 7; } if (variant == 2) { so.shuffleSameTick = true; so.seed = 11; }
        if (variant == 3) { so.tick = 1.0/60; so.reverseTick = true; }
        if (variant == 4) { so.tick = 1.0/30; so.reverseTick = true; }
        if (variant == 5) { so.dropAlpha = 0.03; so.dropSpawn = 0.03; so.seed = 5; }
        sim::Sim Sm; Sm.init(chain); std::mt19937 rng(so.seed);
        double avg = 0, mn = 1e9; int worst = -1, cnt = 0;
        for (int rep = 0; rep < 2; ++rep) {
            double t = rep * plan.seconds();
            for (int i = 0; i < plan.frames; ++i) {
                Sm.run(t + 1e-4 + (mode == 1 ? 0 : 0), so, rng);
                Image r = Sm.render(0, 0, ppu, W, H, so, rng);
                Image tgt = resizeArea(orig[size_t(i)], W, H);
                double ps = computePsnr(tgt, r, 1);
                if (rep == 1 && variant == 0 && i == 20) { auto dump=[&](const Image& im,const char* fn){ std::ofstream o2(fn, std::ios::binary); o2 << "P6\n" << im.w << " " << im.h << "\n255\n"; for (size_t k = 0; k < size_t(im.w)*im.h; ++k) { o2.put(char(im.rgba[k*4])); o2.put(char(im.rgba[k*4+1])); o2.put(char(im.rgba[k*4+2])); } }; dump(r,"/tmp/sim20.ppm"); dump(tgt,"/tmp/tgt20.ppm"); Image direct = renderShapes(res[size_t(plan.group[20])].shapes, res[size_t(plan.group[20])].rasterW, res[size_t(plan.group[20])].rasterH); Image d2 = resizeArea(direct, W, H); printf("   frame20: sim %.1f dB, direct-render %.1f dB\n", ps, computePsnr(tgt, d2, 1)); }
                if (rep == 1) { avg += ps; ++cnt; if (ps < mn) { mn = ps; worst = i; } }
                t += dt;
            }
        }
        printf("  sim variante %d: avg %.1f dB  min %.1f dB (frame %d)\n", variant, avg / cnt, mn, worst);
    }
}
