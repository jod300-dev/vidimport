// ============================================================================
//  vx :: los 7 metodos de animacion (cada uno codifica UNA escena)
// ============================================================================
#pragma once
#include <map>
#include <set>
#include "out.hpp"
#include "paint.hpp"

namespace vx {

using Rgb = std::array<uint8_t, 3>;

struct Shared {                       // recursos que se REUTILIZAN entre escenas (solo una se ve a la vez)
    std::vector<int> channels;        // canales de color libres
    std::vector<int> pulsePool;       // grupos de las teselas del mosaico de pulsos
    int channelsPeak = 0;
    int pool(Out& o, size_t i) { while (pulsePool.size() <= i) pulsePool.push_back(o.group()); return pulsePool[i]; }
};

struct ShotCtx {
    const Params& P; const Clip& clip; Out& out; Shelf& shelf; Shared& sh;
    int a = 0, b = 0;                 // frames [a, b)
    double t0 = 0, dt = 0.04;         // inicio de la escena en el bucle y duracion de un frame
    int budget = 1000;                // objetos (formas + triggers) de esta escena
    int groupReserve = 0;             // grupos que hay que dejar libres para las escenas siguientes
    double frameW = 569, frameH = 320;
    std::vector<char> hold;           // hold[i] = el frame i es (casi) igual al i-1
    std::vector<std::array<int, 2>> offs;   // desplazamiento acumulado de camara (panoramica)
    ShotStats st;
    ShotInfo info;
    std::vector<double> psnr;         // PSNR de cada frame de la escena
    Progress* prog = nullptr;
    ShotCtx(const Params& p, const Clip& c, Out& o, Shelf& s, Shared& h) : P(p), clip(c), out(o), shelf(s), sh(h) {}
    int n() const { return b - a; }
    const Image& fr(int i) const { return clip.frames[size_t(a + i)]; }
    double tf(int i) const { return t0 + i * dt; }
    double eps() const { return std::min(0.03, 0.8 * dt); }
    int evCost() const { return P.reassert ? 2 : 1; }
    ShapeFmt fmt(const Shelf::Slot& s, int W, int H) const {
        ShapeFmt f; f.cx = s.cx; f.cy = s.cy; f.W = W; f.H = H;
        f.ux = frameW / double(clip.w); f.uy = frameH / double(clip.h);
        return f;
    }
    void cancelCheck() const {}
};

// Pasos de la escena: cada paso es un grupo "spawn triggered" que se lanza en un instante. clave = indice de frame (0 = inicio de escena),
// -1 = "inicio + eps" (para que no choque con los triggers del paso 0 sobre el mismo canal/objeto).
struct StepBook {
    ShotCtx& S; std::map<int, int> grp; std::set<int> colorSteps; std::map<int, double> at;   // at: instante propio de un paso (si no, el del frame)
    explicit StepBook(ShotCtx& s) : S(s) {}
    int get(int key) { auto it = grp.find(key); if (it != grp.end()) return it->second; int g = S.out.group(); grp[key] = g; return g; }
    bool has(int key) const { return grp.count(key) > 0; }
    void markColor(int key) { colorSteps.insert(key); }
    double time(int key) const { auto it = at.find(key); if (it != at.end()) return it->second; return key < 0 ? S.t0 + S.eps() : S.tf(key); }
    void flush() { for (auto& [k, g] : grp) S.out.event(time(k), g, k == 0 || !colorSteps.count(k), S.dt); }
};


inline double frameBlendPsnr(const Image& a, const Image& b, const Image& t, double alpha) {
    Image m = a;
    for (size_t i = 0; i < m.rgba.size(); ++i) m.rgba[i] = uint8_t(a.rgba[i] * (1.0 - alpha) + b.rgba[i] * alpha + 0.5);
    return psnrRGB(m, t);
}

// ------------------------------------------------------ 1. CINTA DE CAMARA ---

inline void codeReel(ShotCtx& S) {
    const Params& P = S.P; const int n = S.n(); S.psnr.assign(size_t(n), 0.0);
    std::vector<int> paint;
    for (int i = 0; i < n; ++i) if (i == 0 || !S.hold[size_t(i)]) paint.push_back(i);
    const int nP = int(paint.size());
    const long avail = std::max(1L, long(S.budget) - long(nP) * (1 + S.evCost()));
    std::vector<double> w(size_t(nP), 1.0); double ws = 0;
    for (int k = 0; k < nP; ++k) { w[size_t(k)] = 4.0 + complexityOf(S.fr(paint[size_t(k)])); ws += w[size_t(k)]; }
    const double mean = ws / nP; ws = 0;
    for (auto& v : w) { v = std::clamp(v / mean, 0.55, 1.9); ws += v; }
    std::vector<Result> res(static_cast<size_t>(nP));
    parallelFor(nP, P.threads, [&](int k) {
        int bud = std::max(8, int(double(avail) * w[size_t(k)] / ws));
        res[size_t(k)] = paintFull(S.fr(paint[size_t(k)]), bud, P);
    });
    int pi = 0; double last = 0;
    for (int i = 0; i < n; ++i) {
        if (pi < nP && paint[size_t(pi)] == i) {
            Result& r = res[size_t(pi)];
            const int g = S.out.group();
            Shelf::Slot slot = S.shelf.alloc(S.frameW, S.frameH);
            S.out.shapes(r.shapes, S.fmt(slot, S.clip.w, S.clip.h));
            S.out.camera(Out::gs(g), slot.cx, slot.cy, g, 0);              // el trigger es su propio ancla
            S.out.event(S.tf(i), g, true, S.dt);
            last = r.psnr; ++pi;
        }
        S.psnr[size_t(i)] = last;
    }
    S.info.note = std::to_string(nP) + " cuadros";
}

// ------------------------------------------------------------- MOSAICO -------

struct MosaicPlan {
    std::vector<Cell> cells;
    std::vector<std::vector<Rgb>> target;                         // [frame][celda] color medio exacto
    std::vector<std::vector<std::pair<int, Rgb>>> changes;        // [frame >= 1] celdas que cambian (lazo cerrado)
    std::vector<std::vector<Rgb>> state;                          // [frame] color que tiene cada celda en pantalla
    int tol = 0; long triggers = 0;
};

inline bool planMosaic(ShotCtx& S, int K, long allowed, MosaicPlan& mp) {
    const int n = S.n();
    std::vector<const Image*> sample;
    const int ns = std::min(n, 12);
    for (int i = 0; i < ns; ++i) sample.push_back(&S.fr(int(int64_t(i) * (n - 1) / std::max(1, ns - 1))));
    mp.cells = temporalCells(sample, K, 2, nullptr);
    const size_t C = mp.cells.size();
    mp.target.assign(size_t(n), {});
    parallelFor(n, S.P.threads, [&](int i) { mp.target[size_t(i)] = cellMeans(S.fr(i), mp.cells); });
    static const int tols[] = {2, 4, 6, 9, 13, 18, 25, 34, 45, 60};
    for (int tol : tols) {
        long cnt = 0;
        std::vector<Rgb> st = mp.target[0];
        for (int i = 1; i < n && cnt <= allowed; ++i)
            for (size_t c = 0; c < C; ++c) {
                const Rgb& t = mp.target[size_t(i)][c]; Rgb& s = st[c];
                if (std::max({std::abs(int(t[0]) - s[0]), std::abs(int(t[1]) - s[1]), std::abs(int(t[2]) - s[2])}) > tol) { s = t; ++cnt; }
            }
        if (cnt <= allowed) {
            mp.tol = tol; mp.triggers = cnt;
            mp.changes.assign(size_t(n), {}); mp.state.assign(size_t(n), {});
            std::vector<Rgb> cur = mp.target[0]; mp.state[0] = cur;
            for (int i = 1; i < n; ++i) {
                for (size_t c = 0; c < C; ++c) {
                    const Rgb& t = mp.target[size_t(i)][c]; Rgb& s = cur[c];
                    if (std::max({std::abs(int(t[0]) - s[0]), std::abs(int(t[1]) - s[1]), std::abs(int(t[2]) - s[2])}) > tol) { s = t; mp.changes[size_t(i)].push_back({int(c), t}); }
                }
                mp.state[size_t(i)] = cur;
            }
            return true;
        }
    }
    return false;
}

// Escribe las teselas y los Color/Pulse triggers de un plan de mosaico dentro de los pasos `sb`. El paso 0 ya debe llevar la camara.
inline void emitMosaic(ShotCtx& S, StepBook& sb, const MosaicPlan& mp, const Shelf::Slot& slot, bool pulse, bool tween) {
    Out& o = S.out; const size_t C = mp.cells.size(); const int n = S.n();
    ShapeFmt f = S.fmt(slot, S.clip.w, S.clip.h);
    std::vector<int> pool;
    for (size_t c = 0; c < C; ++c) {
        Shape s; s.cx = (mp.cells[c].x0 + mp.cells[c].x1) * 0.5f; s.cy = (mp.cells[c].y0 + mp.cells[c].y1) * 0.5f;
        s.w = float(mp.cells[c].x1 - mp.cells[c].x0); s.h = float(mp.cells[c].y1 - mp.cells[c].y0);
        if (pulse) { int g = S.sh.pool(o, c); f.groups = Out::gs(g); f.fixedChannel = -1; o.shape(s, f); pool.push_back(g); }
        else { f.fixedChannel = S.sh.channels[c]; o.shape(s, f); }
    }
    const double tEnd = S.tf(n);
    auto put = [&](int key, size_t c, const Rgb& col, double fade) {
        const std::string own = Out::gs(sb.get(key));
        if (pulse) o.pulseGroup(own, pool[c], col, std::max(0.1, tEnd - sb.time(key)) + 1.0);
        else o.color(own, S.sh.channels[c], col, fade);
    };
    for (size_t c = 0; c < C; ++c) put(0, c, mp.target[0][c], 0.0);                    // estado inicial (fade 0)
    for (int i = 1; i < n; ++i) {
        if (mp.changes[size_t(i)].empty()) continue;
        int key = i; double fade = 0;
        if (tween && !pulse) { key = i == 1 ? -1 : i - 1; fade = i == 1 ? S.dt - S.eps() : S.dt; sb.markColor(key); }
        else if (pulse) sb.markColor(key);
        for (auto& cc : mp.changes[size_t(i)]) put(key, size_t(cc.first), cc.second, std::max(fade, 0.0));
    }
}

inline bool mosaicCore(ShotCtx& S, bool pulse, int Kmax, long budgetForBase, MosaicPlan& mp, int& K) {
    const int n = S.n();
    K = Kmax;
    for (int tries = 0; tries < 8 && K >= 24; ++tries) {
        const long fixedCost = 2L * K + n + 6;                 // formas + color de arranque + un trigger de paso
        const long allowed = budgetForBase - fixedCost;
        if (allowed >= K / 4 && planMosaic(S, K, allowed, mp)) return true;
        K = int(K * 0.72);
    }
    return false;
}

inline void mosaicPsnr(ShotCtx& S, const MosaicPlan& mp) {
    S.psnr.assign(size_t(S.n()), 0.0);
    Image canvas = blankCanvas(S.clip.w, S.clip.h);
    for (int i = 0; i < S.n(); ++i) { fillCells(canvas, mp.cells, mp.state[size_t(i)]); S.psnr[size_t(i)] = psnrRGB(canvas, S.fr(i)); }
}

inline void codeMosaic(ShotCtx& S, bool pulse) {
    Out& o = S.out; const Params& P = S.P; const int n = S.n();
    int Kmax = pulse ? P.pulseMaxCells : P.mosaicMaxCells;
    if (pulse) {
        const int gfree = kGroupMax - o.nextGroup - S.groupReserve - 3 * n - 10 + int(S.sh.pulsePool.size());
        Kmax = std::min(Kmax, std::max(0, gfree));
    } else Kmax = std::min<int>(Kmax, int(S.sh.channels.size()));
    MosaicPlan mp; int K = 0;
    if (Kmax < 24 || !mosaicCore(S, pulse, Kmax, S.budget, mp, K)) {
        S.info.note = "sin recursos para el mosaico: se usa la cinta"; codeReel(S); S.info.coder = Coder::Reel; return;
    }
    if (!pulse) S.sh.channelsPeak = std::max<int>(S.sh.channelsPeak, int(mp.cells.size()));
    Shelf::Slot slot = S.shelf.alloc(S.frameW, S.frameH);
    StepBook sb(S); const int ga = o.group();
    o.camera(Out::gs(sb.get(0), ga), slot.cx, slot.cy, ga, 0);
    emitMosaic(S, sb, mp, slot, pulse, P.smooth && !pulse);
    sb.flush();
    mosaicPsnr(S, mp);
    S.info.note = std::to_string(mp.cells.size()) + " teselas, " + std::to_string(mp.triggers) + " cambios (umbral " + std::to_string(mp.tol) + ")";
}

// ------------------------------------------------- 3. MOSAICO + DETALLE ------

inline void codeMosaicDetail(ShotCtx& S) {
    Out& o = S.out; const Params& P = S.P; const int n = S.n(), W = S.clip.w, H = S.clip.h;
    const int Kmax = std::min<int>(std::min(P.mosaicMaxCells, 560), int(S.sh.channels.size()) - (P.channels ? std::min(P.paletteK, 200) : 0));
    MosaicPlan mp; int K = 0;
    if (Kmax < 24 || !mosaicCore(S, false, Kmax, long(S.budget * 0.28), mp, K)) { S.info.note = "sin recursos: se usa la cinta"; codeReel(S); S.info.coder = Coder::Reel; return; }
    const long baseCost = 2L * long(mp.cells.size()) + mp.triggers + n + 6;
    long rem = S.budget - baseCost - long(n) * (2 + S.evCost());           // 2 Toggle + spawn(s) por frame
    rem = std::max(rem, 0L);
    static const int tolD[4] = {26, 20, 15, 11};
    const int td = tolD[std::clamp(P.quality, 0, 3)];
    std::vector<std::vector<Shape>> det(static_cast<size_t>(n)); S.psnr.assign(size_t(n), 0.0);
    Image canvas = blankCanvas(W, H);
    for (int i = 0; i < n; ++i) {
        fillCells(canvas, mp.cells, mp.state[size_t(i)]);
        if (i > 0 && S.hold[size_t(i)]) { S.psnr[size_t(i)] = S.psnr[size_t(i - 1)]; continue; }
        int bd = std::max(0, int(0.93 * double(rem) / double(n - i)));
        if (bd >= 6) { det[size_t(i)] = residualShapes(S.fr(i), canvas, td, bd, P); rem -= long(det[size_t(i)].size()); }
        Image c2 = canvas; paintOnto(c2, det[size_t(i)]);
        S.psnr[size_t(i)] = psnrRGB(c2, S.fr(i));
    }
    // paleta de canales del detalle (opcional)
    std::map<uint32_t, int> pal; int palN = 0;
    if (P.channels) {
        std::vector<Shape> all; for (auto& v : det) all.insert(all.end(), v.begin(), v.end());
        const int avail = int(S.sh.channels.size()) - int(mp.cells.size());
        if (!all.empty() && avail >= 2) {
            gdx::quantizeShapesToPalette(all, std::min(P.paletteK, avail));
            size_t k = 0; for (auto& v : det) for (auto& s : v) s = all[k++];
            for (auto& s : all) { uint32_t key = gdx::ChannelMap::key(s.r, s.g, s.b); if (!pal.count(key)) { const size_t idx = mp.cells.size() + pal.size(); pal[key] = S.sh.channels[idx]; } }
            palN = int(pal.size());
        }
    }
    S.sh.channelsPeak = std::max<int>(S.sh.channelsPeak, int(mp.cells.size()) + palN);
    Shelf::Slot slot = S.shelf.alloc(S.frameW, S.frameH);
    StepBook sb(S); const int ga = o.group(), Q = o.group();
    const int g0 = sb.get(0);
    o.camera(Out::gs(g0, ga), slot.cx, slot.cy, ga, 0);
    emitMosaic(S, sb, mp, slot, false, P.smooth);
    for (auto& kv : pal) { Rgb c = {uint8_t(kv.first >> 16), uint8_t(kv.first >> 8), uint8_t(kv.first)}; o.color(Out::gs(g0), kv.second, c, 0.0); }
    ShapeFmt f = S.fmt(slot, W, H); f.zBase = 1; f.palette = pal.empty() ? nullptr : &pal;
    int cur = -1; bool qOff = false;
    std::vector<int> dg(size_t(n), 0);
    for (int i = 0; i < n; ++i) if (!det[size_t(i)].empty()) dg[size_t(i)] = o.group();
    for (int i = 0; i < n; ++i) {
        if (dg[size_t(i)]) { f.groups = i == 0 ? Out::gs(dg[size_t(i)]) : Out::gs(dg[size_t(i)], Q); o.shapes(det[size_t(i)], f); qOff = true; }
    }
    if (qOff) o.toggle(Out::gs(g0), Q, false);                         // al empezar la escena: todos los detalles (menos el del frame 0) apagados
    for (int i = 0; i < n; ++i) {
        if (i > 0 && S.hold[size_t(i)]) continue;
        const int g = dg[size_t(i)];
        if (i == 0) { if (g) { o.toggle(Out::gs(g0), g, true); cur = g; } continue; }
        if (!g && cur < 0) continue;
        const std::string own = Out::gs(sb.get(i));
        if (cur >= 0 && cur != g) o.toggle(own, cur, false);
        if (g) o.toggle(own, g, true);
        cur = g ? g : -1;
    }
    sb.flush();
    S.info.note = std::to_string(mp.cells.size()) + " teselas + detalle";
}

// ------------------------------------------------------ 4. EPOCAS DE PARCHES --

struct EpochData { int f0 = 0, f1 = 0; std::vector<Shape> base; std::vector<std::vector<Shape>> patch; };

inline void codeEpochs(ShotCtx& S) {
    Out& o = S.out; const Params& P = S.P; const int n = S.n(), W = S.clip.w, H = S.clip.h;
    static const int tolP[4] = {22, 17, 13, 9};
    const int tp = tolP[std::clamp(P.quality, 0, 3)];
    const int kMaxLen = 20;
    long rem = long(S.budget) - long(n) * (1 + S.evCost()) - 10 - 6L * (n / kMaxLen + 1);
    rem = std::max(rem, 100L);
    std::vector<EpochData> eps; S.psnr.assign(size_t(n), 0.0);
    Image canvas;
    int f = 0;
    while (f < n) {
        EpochData ep; ep.f0 = f;
        const int framesLeft = n - f, epochsLeft = (framesLeft + kMaxLen - 1) / kMaxLen;
        double u = 0.92 * double(rem) / double(framesLeft + 3 * epochsLeft);
        const int bKey = int(std::clamp(4.0 * u, 40.0, 3500.0));
        Result kr = paintFull(S.fr(f), bKey, P);
        ep.base = kr.shapes; rem -= long(ep.base.size());
        canvas = blankCanvas(W, H); paintOnto(canvas, ep.base);
        S.psnr[size_t(f)] = psnrRGB(canvas, S.fr(f));
        ep.patch.assign(1, {});
        int g = f + 1;
        for (; g < n && g - f < kMaxLen; ++g) {
            if (S.hold[size_t(g)]) { ep.patch.push_back({}); S.psnr[size_t(g)] = S.psnr[size_t(g - 1)]; continue; }
            double dirty = 0;
            const int left = n - g;
            const double ul = 0.92 * double(rem) / double(left + 3 * ((left + kMaxLen - 1) / kMaxLen));
            const int bp = std::max(0, int(ul));
            std::vector<Shape> pt = residualShapes(S.fr(g), canvas, tp, bp, P, &dirty);
            if (dirty > 0.45 && g - f >= 2) break;                       // casi todo cambio: nueva epoca (nuevo keyframe)
            for (auto& s : pt) s.z = int16_t(std::min(30000, g - f));
            paintOnto(canvas, pt); rem -= long(pt.size());
            ep.patch.push_back(std::move(pt));
            S.psnr[size_t(g)] = psnrRGB(canvas, S.fr(g));
            if (S.psnr[size_t(g)] < 21.0 && g - f >= 3) { ++g; break; }
        }
        ep.f1 = f + int(ep.patch.size());
        f = ep.f1;
        eps.push_back(std::move(ep));
    }
    // ---- emision
    Shelf::Slot slot = S.shelf.alloc(S.frameW, S.frameH);
    StepBook sb(S); const int ga = o.group(), g0 = sb.get(0);
    o.camera(Out::gs(g0, ga), slot.cx, slot.cy, ga, 0);
    ShapeFmt fm = S.fmt(slot, W, H);
    const int E = int(eps.size());
    std::vector<int> B(static_cast<size_t>(E)), Q(static_cast<size_t>(E));
    for (int k = 0; k < E; ++k) { B[size_t(k)] = o.group(); Q[size_t(k)] = o.group(); }
    for (int k = 0; k < E; ++k) {
        fm.groups = Out::gs(B[size_t(k)]); fm.zBase = 0; o.shapes(eps[size_t(k)].base, fm);
        for (size_t j = 1; j < eps[size_t(k)].patch.size(); ++j) {
            auto& pt = eps[size_t(k)].patch[j]; if (pt.empty()) continue;
            const int pg = o.group(); fm.groups = Out::gs(pg, Q[size_t(k)]);
            o.shapes(pt, fm);
            o.toggle(Out::gs(sb.get(eps[size_t(k)].f0 + int(j))), pg, true);
        }
    }
    const std::string own0 = Out::gs(g0);
    for (int k = 0; k < E; ++k) { o.toggle(own0, Q[size_t(k)], false); if (k > 0) o.toggle(own0, B[size_t(k)], false); }
    o.toggle(own0, B[0], true);
    for (int k = 1; k < E; ++k) {
        const std::string own = Out::gs(sb.get(eps[size_t(k)].f0));
        o.toggle(own, B[size_t(k - 1)], false); o.toggle(own, Q[size_t(k - 1)], false); o.toggle(own, B[size_t(k)], true);
    }
    sb.flush();
    S.info.note = std::to_string(E) + " epocas";
}

// ------------------------------------------------------- 5. PANORAMICA --------

inline void codePanorama(ShotCtx& S) {
    Out& o = S.out; const Params& P = S.P; const int n = S.n(), W = S.clip.w, H = S.clip.h;
    if (!S.st.panOk || int(S.offs.size()) != n) { S.info.note = "panorama inviable: epocas"; codeEpochs(S); S.info.coder = Coder::Epochs; return; }
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (auto& v : S.offs) { minX = std::min(minX, v[0]); maxX = std::max(maxX, v[0]); minY = std::min(minY, v[1]); maxY = std::max(maxY, v[1]); }
    const int PW = W + (maxX - minX), PH = H + (maxY - minY);
    auto pos = [&](int i) { return std::array<int, 2>{S.offs[size_t(i)][0] - minX, S.offs[size_t(i)][1] - minY}; };
    // panorama = mediana temporal de los frames alineados (quita lo que se mueve)
    const int ns = std::min(n, 15);
    std::vector<int> sidx; for (int i = 0; i < ns; ++i) sidx.push_back(int(int64_t(i) * (n - 1) / std::max(1, ns - 1)));
    std::vector<uint8_t> acc(size_t(PW) * PH * size_t(ns) * 3); std::vector<uint8_t> cnt(size_t(PW) * PH, 0);
    for (int i : sidx) {
        auto p = pos(i);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                size_t pi = size_t(p[1] + y) * PW + (p[0] + x); const uint8_t* q = S.fr(i).at(x, y);
                size_t base = (pi * size_t(ns) + cnt[pi]) * 3; acc[base] = q[0]; acc[base + 1] = q[1]; acc[base + 2] = q[2]; ++cnt[pi];
            }
    }
    Image pano = blankCanvas(PW, PH);
    for (size_t pi = 0; pi < size_t(PW) * PH; ++pi) {
        if (!cnt[pi]) continue;
        for (int c = 0; c < 3; ++c) {
            uint8_t v[16]; for (int k = 0; k < cnt[pi]; ++k) v[k] = acc[(pi * size_t(ns) + k) * 3 + c];
            std::nth_element(v, v + cnt[pi] / 2, v + cnt[pi]); pano.rgba[pi * 4 + c] = v[cnt[pi] / 2];
        }
        pano.rgba[pi * 4 + 3] = 255;
    }
    const double areaF = double(PW) * PH / (double(W) * H);
    const long fixed = long(n) * (2 + S.evCost() + 1) + 10;
    const long avail = std::max(100L, long(S.budget) - fixed);
    const int bPano = int(std::min(avail * 0.62, avail * 0.28 * std::sqrt(areaF)));
    gdx::Options po = paintOptions(P, bPano, PW, PH);
    Result pr = gdx::convert(pano, gdx::Method::BSP, po, nullptr); rescaleTo(pr, PW, PH);
    for (auto& s : pr.shapes) s.z = 0;
    Image pcanvas = blankCanvas(PW, PH); paintOnto(pcanvas, pr.shapes);
    long rem = avail - long(pr.shapes.size());
    static const int tolP[4] = {22, 17, 13, 9};
    const int tp = tolP[std::clamp(P.quality, 0, 3)];
    std::vector<std::vector<Shape>> det(static_cast<size_t>(n)); S.psnr.assign(size_t(n), 0.0);
    bool any = false;
    for (int i = 0; i < n; ++i) {
        auto p = pos(i);
        Image crop = blankCanvas(W, H);
        for (int y = 0; y < H; ++y) std::memcpy(crop.at(0, y), pcanvas.at(p[0], p[1] + y), size_t(W) * 4);
        if (i > 0 && S.hold[size_t(i)] && S.offs[size_t(i)] == S.offs[size_t(i - 1)]) { S.psnr[size_t(i)] = S.psnr[size_t(i - 1)]; det[size_t(i)] = det[size_t(i - 1)]; continue; }
        const int bd = std::max(0, int(0.93 * double(rem) / double(n - i)));
        std::vector<Shape> d = bd >= 6 ? residualShapes(S.fr(i), crop, tp, bd, P) : std::vector<Shape>{};
        rem -= long(d.size());
        paintOnto(crop, d); S.psnr[size_t(i)] = psnrRGB(crop, S.fr(i));
        for (auto& s : d) { s.cx += float(p[0]); s.cy += float(p[1]); }
        if (!d.empty()) any = true;
        det[size_t(i)] = std::move(d);
    }
    // ---- emision
    const double ux = S.frameW / double(W), uy = S.frameH / double(H);
    Shelf::Slot slot = S.shelf.alloc(PW * ux, PH * uy);
    ShapeFmt fm; fm.cx = slot.cx; fm.cy = slot.cy; fm.W = PW; fm.H = PH; fm.ux = ux; fm.uy = uy;
    o.shapes(pr.shapes, fm);
    StepBook sb(S); const int g0 = sb.get(0), Q = o.group();
    auto camPos = [&](int i, double& x, double& y) { auto p = pos(i); x = slot.cx + ((p[0] + W * 0.5) - PW * 0.5) * ux; y = slot.cy - ((p[1] + H * 0.5) - PH * 0.5) * uy; };
    const bool smooth = P.smooth && !any;                             // sin residuo la camara puede deslizarse suave entre frames
    std::vector<int> dg(size_t(n), 0);
    for (int i = 0; i < n; ++i) if (!det[size_t(i)].empty() && !(i > 0 && S.hold[size_t(i)] && dg[size_t(i - 1)])) dg[size_t(i)] = o.group();
    fm.zBase = 1;
    for (int i = 0; i < n; ++i) if (dg[size_t(i)]) { fm.groups = i == 0 ? Out::gs(dg[size_t(i)]) : Out::gs(dg[size_t(i)], Q); o.shapes(det[size_t(i)], fm); }
    { double x, y; camPos(0, x, y); const int ga = o.group(); o.camera(Out::gs(g0, ga), x, y, ga, 0); }
    if (any) o.toggle(Out::gs(g0), Q, false);
    int cur = dg[0]; if (cur) o.toggle(Out::gs(g0), cur, true);
    for (int i = 1; i < n; ++i) {
        const bool move = S.offs[size_t(i)] != S.offs[size_t(i - 1)];
        if (move) {
            double x, y; camPos(i, x, y); const int ga = o.group();
            const int key = smooth ? (i == 1 ? -1 : i - 1) : i;
            const double fade = smooth ? (i == 1 ? S.dt - S.eps() : S.dt) : 0.0;
            if (smooth) sb.markColor(key);                           // el desliz no se relanza
            o.camera(Out::gs(sb.get(key), ga), x, y, ga, fade);
        }
        if (S.hold[size_t(i)] && !dg[size_t(i)]) continue;
        const int g = dg[size_t(i)];
        if (g == cur) continue;
        const std::string own = Out::gs(sb.get(i));
        if (cur > 0 && cur != g) o.toggle(own, cur, false);
        if (g) o.toggle(own, g, true);
        cur = g;
    }
    sb.flush();
    S.info.note = "panorama " + std::to_string(PW) + "x" + std::to_string(PH) + (smooth ? ", camara suave" : "");
}

// ------------------------------------------------------ 6. FUNDIDO CRUZADO ----

inline void codeCrossfade(ShotCtx& S) {
    Out& o = S.out; const Params& P = S.P; const int n = S.n(), W = S.clip.w, H = S.clip.h;
    int k = P.kfStep > 0 ? P.kfStep : (P.fps >= 24 ? 3 : 1);
    k = std::clamp(k, 1, 8);
    std::vector<int> keys; for (int i = 0; i < n; i += k) keys.push_back(i);
    if (keys.size() < 2) { S.info.note = "escena corta: cinta"; codeReel(S); S.info.coder = Coder::Reel; return; }
    const int nk = int(keys.size());
    const long avail = std::max(1L, long(S.budget) - long(nk) * (6 + S.evCost()) - 12);
    std::vector<Result> kr(static_cast<size_t>(nk));
    const int bk = int(std::max<long>(8, avail / nk));
    parallelFor(nk, P.threads, [&](int j) { kr[size_t(j)] = paintFull(S.fr(keys[size_t(j)]), bk, P); });
    std::vector<Image> ren(static_cast<size_t>(nk));
    for (int j = 0; j < nk; ++j) ren[size_t(j)] = gdx::renderShapes(kr[size_t(j)].shapes, W, H);
    S.psnr.assign(size_t(n), 0.0);
    for (int i = 0; i < n; ++i) {
        int j = std::min(nk - 1, i / k);
        if (j == nk - 1 || i % k == 0) S.psnr[size_t(i)] = psnrRGB(ren[size_t(j)], S.fr(i));
        else S.psnr[size_t(i)] = frameBlendPsnr(ren[size_t(j)], ren[size_t(j + 1)], S.fr(i), double(i % k) / k);
    }
    // ---- emision
    Shelf::Slot slot = S.shelf.alloc(S.frameW, S.frameH);
    StepBook sb(S); const int g0 = sb.get(0), ga = o.group(), Z = o.group();
    std::vector<int> K(static_cast<size_t>(nk));
    for (int j = 0; j < nk; ++j) K[size_t(j)] = o.group();
    ShapeFmt fm = S.fmt(slot, W, H); int zb = 0;
    for (int j = 0; j < nk; ++j) {
        int span = 1; for (auto& s : kr[size_t(j)].shapes) span = std::max<int>(span, s.z + 1);
        fm.zBase = zb; zb += span;
        fm.groups = j == 0 ? Out::gs(K[0]) : Out::gs(K[size_t(j)], Z);
        o.shapes(kr[size_t(j)].shapes, fm);
    }
    const double d = std::max(0.02, k * S.dt * 0.92);
    o.camera(Out::gs(g0, ga), slot.cx, slot.cy, ga, 0);
    o.toggle(Out::gs(g0), Z, false); o.alpha(Out::gs(g0), Z, 0, 0.0); o.toggle(Out::gs(g0), K[0], true);
    for (int j = 1; j < nk; ++j) {
        const int key = keys[size_t(j)]; const int g = sb.get(key);
        sb.at[key] = std::max(S.t0 + S.eps(), S.tf(key) - d);          // el fundido TERMINA justo en el instante del keyframe (sin retraso)
        o.toggle(Out::gs(g), K[size_t(j)], true); o.alpha(Out::gs(g), K[size_t(j)], 1, d);
        const int off = o.group();
        o.toggle(Out::gs(off), K[size_t(j - 1)], false);
        o.spawn(Out::gs(g), off, d + 0.01);
        sb.markColor(key);                                           // un fundido no se relanza
    }
    sb.flush();
    S.info.note = std::to_string(nk) + " keyframes cada " + std::to_string(k) + " frames";
}

} // namespace vx
