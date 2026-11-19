// ============================================================================
//  vx :: Director. Parte el video en escenas, reparte el presupuesto de objetos, elige el metodo de cada escena y monta el nivel.
// ============================================================================
#pragma once
#include "coders.hpp"

namespace vx {

// El metodo 8 (automatico): reglas sobre las estadisticas de la escena y su presupuesto por frame.
inline Coder chooseCoder(const ShotCtx& S) {
    const int n = S.n();
    int paid = 0; for (int i = 0; i < n; ++i) if (i == 0 || !S.hold[size_t(i)]) ++paid;     // los frames repetidos no cuestan objetos
    const double b = double(S.budget) / std::max(1, paid);
    const ShotStats& st = S.st;
    if (n <= 2 || paid <= 2) return Coder::Reel;
    if (b >= 1700) return Coder::Reel;                                              // presupuesto sobrado: cada frame completo
    const bool pan = st.panOk && st.panPx >= 1.2 && st.residualAligned < 0.30;
    if (pan && b >= 90) return Coder::Panorama;
    const bool still = st.residualAligned < 0.09 && st.panPx < 1.2;
    if (b < 90) return Coder::Mosaic;
    if (still && b >= 160) return Coder::Epochs;                                    // poco cambia: deltas baratas
    if (st.panPx < 1.2 && st.moving < 0.38 && st.canvasArea < 1.3 && b >= 220) return Coder::Panorama;   // fondo fijo + objeto que se mueve
    if (b < 420) return Coder::MosaicDetail;
    if (st.motion < 7.0 && S.P.fps >= 24 && b < 1100) return Coder::Crossfade;     // contenido lento: interpolar keyframes
    if (b >= 900) return Coder::Reel;
    return Coder::Epochs;
}

inline void frameBox(int w, int h, double& fw, double& fh) {                        // el cuadro cabe en 560 x 320 u (pantalla 16:9 a zoom 1)
    const double asp = h > 0 ? double(w) / h : 1.0;
    fh = kScreenH; fw = fh * asp;
    if (fw > 560.0) { fh *= 560.0 / fw; fw = 560.0; }
}

inline Output encodeShots(const Clip& clip, const Params& Pin, const std::vector<Shot>& shots, Progress* prog = nullptr) {
    Output res; const double t0ms = gdx::nowMs();
    Params P = Pin; P.fps = int(std::lround(clip.fps)); P.budget = std::max(2000, P.budget);
    if (!clip.ok()) { res.error = "El video no tiene frames suficientes"; return res; }
    Out out(P); Shelf shelf; shelf.x0 = P.originX; shelf.y0 = P.originY;
    Shared sh;
    if (P.freeChannels.empty()) for (int c = 2; c <= kChannelMax; ++c) sh.channels.push_back(c);
    else for (int c : P.freeChannels) if (c >= 2 && c <= kChannelMax) sh.channels.push_back(c);
    const double dt = 1.0 / clip.fps; double fw, fh; frameBox(clip.w, clip.h, fw, fh);
    const int N = clip.count();
    res.firstGroup = std::max(1, P.firstGroup);

    // ---- estadisticas, frames repetidos y reparto del presupuesto
    const int S_ = int(shots.size());
    std::vector<ShotStats> stats(static_cast<size_t>(S_)); std::vector<std::vector<std::array<int, 2>>> offs(static_cast<size_t>(S_));
    std::vector<std::vector<char>> holds(static_cast<size_t>(S_));
    std::vector<double> weight(size_t(S_), 1.0); double wsum = 0, cmean = 0;
    for (int s = 0; s < S_; ++s) {
        const Shot& sh0 = shots[size_t(s)]; const int n = sh0.b - sh0.a;
        const bool needStats = P.coder == Coder::Auto || P.coder == Coder::Panorama || (size_t(s) < P.forced.size() && P.forced[size_t(s)] == int(Coder::Panorama));
        if (needStats) stats[size_t(s)] = analyzeShot(clip, sh0, &offs[size_t(s)]);
        else stats[size_t(s)].complexity = complexityOf(clip.frames[size_t(sh0.a + n / 2)]);
        holds[size_t(s)].assign(size_t(n), 0);
        for (int i = 1; i < n; ++i) holds[size_t(s)][size_t(i)] = meanAbsDiff(clip.frames[size_t(sh0.a + i - 1)], clip.frames[size_t(sh0.a + i)], 2) < 0.7;
        cmean += stats[size_t(s)].complexity * n;
        gdx::report(prog, 0.05f * float(s + 1) / float(S_));
    }
    cmean = std::max(1.0, cmean / N);
    for (int s = 0; s < S_; ++s) {
        const int n = shots[size_t(s)].b - shots[size_t(s)].a;
        int holdN = 0; for (char h : holds[size_t(s)]) holdN += h;
        weight[size_t(s)] = (std::max(1.0, double(n - holdN)) + 3.0) * (0.55 + std::min(1.6, stats[size_t(s)].complexity / cmean) * 0.45);
        wsum += weight[size_t(s)];
    }
    const long overhead = 24 + long(N) / 40;
    double poolLeft = 0.985 * double(std::max<long>(500, long(P.budget) - overhead)), wLeft = wsum;     // lo que una escena no gasta pasa a las siguientes

    // ---- codificar escena a escena
    res.framePsnr.assign(size_t(N), 0.0);
    int groupsAfter = 0; for (int s = 0; s < S_; ++s) groupsAfter += 3 * (shots[size_t(s)].b - shots[size_t(s)].a);
    std::vector<int> order(static_cast<size_t>(S_)); for (int s = 0; s < S_; ++s) order[size_t(s)] = s;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return weight[size_t(x)] < weight[size_t(y)]; });   // primero las baratas: lo que no gastan pasa a las caras
    for (int oi = 0; oi < S_; ++oi) {
        const int s = order[size_t(oi)];
        const Shot& sh0 = shots[size_t(s)];
        ShotCtx S(P, clip, out, shelf, sh);
        S.a = sh0.a; S.b = sh0.b; S.t0 = sh0.a * dt; S.dt = dt; S.frameW = fw; S.frameH = fh;
        S.budget = std::max(30, int(poolLeft * weight[size_t(s)] / std::max(1e-9, wLeft)));
        groupsAfter -= 3 * S.n(); S.groupReserve = groupsAfter;
        S.hold = holds[size_t(s)]; S.offs = offs[size_t(s)]; S.st = stats[size_t(s)]; S.prog = prog;
        Coder c = P.coder;
        if (size_t(s) < P.forced.size() && P.forced[size_t(s)] >= 0 && P.forced[size_t(s)] < int(Coder::Auto)) c = Coder(P.forced[size_t(s)]);
        if (c == Coder::Auto) c = chooseCoder(S);
        S.info.coder = c; S.info.a = S.a; S.info.b = S.b; S.info.t0 = S.t0; S.info.dur = S.n() * dt;
        const size_t sh1 = out.nShape, tr1 = out.nTrig; const int g1 = out.nextGroup;
        switch (c) {
            case Coder::Reel: codeReel(S); break;
            case Coder::Mosaic: codeMosaic(S, false); break;
            case Coder::MosaicDetail: codeMosaicDetail(S); break;
            case Coder::Epochs: codeEpochs(S); break;
            case Coder::Panorama: codePanorama(S); break;
            case Coder::Crossfade: codeCrossfade(S); break;
            case Coder::PulseMosaic: codeMosaic(S, true); break;
            default: codeReel(S); break;
        }
        S.info.shapes = int(out.nShape - sh1); S.info.triggers = int(out.nTrig - tr1); S.info.groups = out.nextGroup - g1;
        double sum = 0, mn = 99; for (double p : S.psnr) { sum += p; mn = std::min(mn, p); }
        S.info.psnr = S.psnr.empty() ? 0 : sum / S.psnr.size(); S.info.minPsnr = mn;
        for (int i = 0; i < S.n() && i < int(S.psnr.size()); ++i) res.framePsnr[size_t(S.a + i)] = S.psnr[size_t(i)];
        poolLeft -= double(S.info.shapes + S.info.triggers); wLeft -= weight[size_t(s)];
        res.shots.push_back(S.info);
        gdx::report(prog, 0.05f + 0.95f * float(oi + 1) / float(S_));
        if (gdx::isCancelled(prog)) { res.error = "Cancelado"; return res; }
        if (out.overflow) { res.error = "No quedan grupos libres (max. 9999): baja la duracion, los fps o usa otro metodo"; return res; }
    }
    std::sort(res.shots.begin(), res.shots.end(), [](const ShotInfo& a, const ShotInfo& b) { return a.a < b.a; });
    out.finish(N * dt);
    if (out.overflow) { res.error = "No quedan grupos libres (max. 9999): baja la duracion o los fps"; return res; }
    res.shapes = out.nShape; res.triggers = out.nTrig; res.groupsUsed = out.nextGroup - res.firstGroup;
    res.channelsUsed = sh.channelsPeak; res.seconds = N * dt;
    double sum = 0, mn = 99; for (double p : res.framePsnr) { sum += p; mn = std::min(mn, p); }
    res.psnr = sum / N; res.minPsnr = mn;
    res.chunks = out.take();
    res.ms = gdx::nowMs() - t0ms;
    if (res.objects() > size_t(P.budget)) res.note = "Se paso del presupuesto en " + std::to_string(res.objects() - size_t(P.budget)) + " objetos";
    return res;
}

inline Output encode(const Clip& clip, const Params& P, Progress* prog = nullptr) {
    return encodeShots(clip, P, detectShots(clip), prog);
}

// ---- Patron de prueba: 7 escenas sinteticas de 1 s, cada una con UN metodo forzado, para comprobar en el juego que todos los triggers
// (camara estatica, Toggle, Alpha, Color con fundido, Pulse) se comportan como espera el mod.
inline Clip makeTestClip(int fps) {
    Clip c; c.w = 96; c.h = 54; c.fps = fps; c.srcFps = fps; c.name = "prueba";
    const int per = fps;                                 // 1 s por escena
    const uint8_t pal[7][3] = {{200, 40, 40}, {40, 160, 60}, {40, 70, 200}, {210, 180, 30}, {160, 50, 170}, {30, 170, 180}, {230, 120, 40}};
    for (int s = 0; s < 7; ++s)
        for (int i = 0; i < per; ++i) {
            Image im; im.w = c.w; im.h = c.h; im.rgba.resize(size_t(c.w) * c.h * 4);
            const double u = double(i) / std::max(1, per - 1);
            for (int y = 0; y < c.h; ++y)
                for (int x = 0; x < c.w; ++x) {
                    uint8_t* p = im.at(x, y); p[3] = 255;
                    int sx = s == 4 ? x + int(u * 30) : x;                                     // escena 5: textura que se desplaza (camara)
                    double tex = 0.5 + 0.5 * std::sin(sx * 0.35) * std::sin(y * 0.4);
                    for (int k = 0; k < 3; ++k) p[k] = uint8_t(pal[s][k] * (0.45 + 0.55 * (s == 4 ? tex : (0.5 + 0.5 * y / c.h))));
                }
            const int bx = 6 + int(u * (c.w - 24)), by = 14 + (s % 2) * 8;                     // cuadrado blanco que avanza
            for (int y = by; y < by + 14; ++y) for (int x = bx; x < bx + 14; ++x) { uint8_t* p = im.at(x, y); p[0] = p[1] = p[2] = 245; }
            if (s == 1) for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) { uint8_t* p = im.at(x, y); p[0] = uint8_t(p[0] * (0.6 + 0.4 * u)); }   // el mosaico se funde
            c.frames.push_back(std::move(im));
        }
    return c;
}

inline Output encodeTestPattern(const Params& Pin, int fps) {
    Clip c = makeTestClip(fps); Params P = Pin; P.budget = 14000; P.coder = Coder::Auto;
    P.forced = {0, 1, 2, 3, 4, 5, 6};
    std::vector<Shot> shots; const int per = fps;
    for (int s = 0; s < 7; ++s) shots.push_back({s * per, (s + 1) * per});
    return encodeShots(c, P, shots, nullptr);
}

} // namespace vx
