// ============================================================================
//  vx :: analisis del video: diferencias entre frames, cortes de escena, movimiento global de camara,
//  y teselacion TEMPORAL (mosaico de rectangulos que minimiza el error sobre todos los frames de una escena).
// ============================================================================
#pragma once
#include <atomic>
#include <cmath>
#include <functional>
#include <queue>
#include <thread>
#include "types.hpp"

namespace vx {

// -------------------------------------------------------- utilidades ----------

template <class F>
inline void parallelFor(int n, int threads, F&& fn) {
    threads = std::clamp(threads, 1, std::max(1, n));
    if (threads == 1) { for (int i = 0; i < n; ++i) fn(i); return; }
    std::atomic<int> next{0};
    auto worker = [&]() { for (;;) { int i = next.fetch_add(1); if (i >= n) return; fn(i); } };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
}

// diferencia absoluta media (0..255) entre dos frames del mismo tamano (muestreo cada `step` pixeles)
inline double meanAbsDiff(const Image& a, const Image& b, int step = 2) {
    if (a.w != b.w || a.h != b.h || a.empty()) return 255.0;
    double s = 0; size_t n = 0;
    for (int y = 0; y < a.h; y += step)
        for (int x = 0; x < a.w; x += step) {
            const uint8_t* p = a.at(x, y); const uint8_t* q = b.at(x, y);
            s += std::abs(int(p[0]) - q[0]) + std::abs(int(p[1]) - q[1]) + std::abs(int(p[2]) - q[2]); ++n;
        }
    return n ? s / (3.0 * double(n)) : 0.0;
}

// fraccion de pixeles cuyo canal mas distinto supera tol
inline double changedFraction(const Image& a, const Image& b, int tol, int step = 2) {
    if (a.w != b.w || a.h != b.h || a.empty()) return 1.0;
    size_t c = 0, n = 0;
    for (int y = 0; y < a.h; y += step)
        for (int x = 0; x < a.w; x += step) {
            const uint8_t* p = a.at(x, y); const uint8_t* q = b.at(x, y);
            int d = std::max({std::abs(int(p[0]) - q[0]), std::abs(int(p[1]) - q[1]), std::abs(int(p[2]) - q[2])});
            if (d > tol) ++c; ++n;
        }
    return n ? double(c) / double(n) : 0.0;
}

// nitidez media (gradiente de luma): mide cuanto detalle tiene un frame
inline double complexityOf(const Image& im) {
    if (im.w < 3 || im.h < 3) return 0;
    double s = 0; size_t n = 0;
    auto luma = [&](int x, int y) { const uint8_t* p = im.at(x, y); return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; };
    for (int y = 1; y < im.h - 1; y += 2)
        for (int x = 1; x < im.w - 1; x += 2) { s += std::abs(luma(x + 1, y) - luma(x - 1, y)) + std::abs(luma(x, y + 1) - luma(x, y - 1)); ++n; }
    return n ? s / double(n) : 0;
}

inline double psnrRGB(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h || a.empty()) return 0;
    double e = 0;
    const size_t n = size_t(a.w) * a.h;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* p = &a.rgba[i * 4]; const uint8_t* q = &b.rgba[i * 4];
        double d0 = double(p[0]) - q[0], d1 = double(p[1]) - q[1], d2 = double(p[2]) - q[2];
        e += d0 * d0 + d1 * d1 + d2 * d2;
    }
    double mse = e / (3.0 * double(n));
    return mse < 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// ------------------------------------------------------------ escenas -------

struct Shot { int a = 0, b = 0; };       // frames [a, b)

struct ShotStats {
    double motion = 0;                    // diferencia media entre frames consecutivos (0..255)
    double moving = 0;                    // fraccion media de pixeles que cambian de verdad
    double complexity = 0;                // detalle medio
    double panPx = 0;                     // desplazamiento medio de camara (px del raster por frame)
    double residualAligned = 1;           // fraccion de pixeles que siguen cambiando TRAS compensar la camara
    double canvasArea = 1;                // area del panorama / area de un frame
    bool panOk = false;                   // el panorama es viable (movimiento de camara de traslacion)
};

inline std::vector<Shot> detectShots(const Clip& c, int minLen = 3) {
    const int n = c.count();
    std::vector<Shot> shots;
    if (n <= 0) return shots;
    std::vector<double> d(size_t(n), 0.0);
    for (int i = 1; i < n; ++i) d[size_t(i)] = meanAbsDiff(c.frames[size_t(i - 1)], c.frames[size_t(i)], 2);
    std::vector<int> cuts{0};
    for (int i = 1; i < n; ++i) {
        std::vector<double> loc;
        for (int k = std::max(1, i - 8); k < i; ++k) loc.push_back(d[size_t(k)]);
        double med = 1.5;
        if (!loc.empty()) { std::nth_element(loc.begin(), loc.begin() + loc.size() / 2, loc.end()); med = std::max(1.5, loc[loc.size() / 2]); }
        // corte = salto grande y claramente por encima del movimiento normal de la escena
        if (d[size_t(i)] > 24.0 && d[size_t(i)] > 3.2 * med && i - cuts.back() >= minLen) cuts.push_back(i);
    }
    cuts.push_back(n);
    for (size_t k = 0; k + 1 < cuts.size(); ++k) shots.push_back({cuts[k], cuts[k + 1]});
    // la ultima escena no puede quedar mas corta que minLen: se funde con la anterior
    if (shots.size() >= 2 && shots.back().b - shots.back().a < minLen) { shots[shots.size() - 2].b = shots.back().b; shots.pop_back(); }
    return shots;
}

// ---------------------------------------------------- movimiento de camara ---

struct Gray {
    int w = 0, h = 0; std::vector<uint8_t> v;
    uint8_t at(int x, int y) const { return v[size_t(y) * w + x]; }
};
inline Gray toGray(const Image& im, int div) {
    Gray g; g.w = std::max(1, im.w / div); g.h = std::max(1, im.h / div); g.v.assign(size_t(g.w) * g.h, 0);
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            int s = 0, c = 0;
            for (int j = 0; j < div; ++j)
                for (int i = 0; i < div; ++i) {
                    int xx = x * div + i, yy = y * div + j;
                    if (xx < im.w && yy < im.h) { const uint8_t* p = im.at(xx, yy); s += (299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000; ++c; }
                }
            g.v[size_t(y) * g.w + x] = uint8_t(c ? s / c : 0);
        }
    return g;
}

// El contenido se movio (mx, my) de `prev` a `cur`:  cur(x, y) ~ prev(x - mx, y - my). Busqueda gruesa en 1/4 y refinado a pixel completo.
inline void estimateMotion(const Gray& prev4, const Gray& cur4, const Gray& prev1, const Gray& cur1, int R, int& mx, int& my) {
    auto sad = [&](const Gray& P, const Gray& C, int sx, int sy, int step, int margin) {
        double s = 0; int n = 0;
        for (int y = margin; y < C.h - margin; y += step)
            for (int x = margin; x < C.w - margin; x += step) {
                int px = x - sx, py = y - sy;
                if (px < 0 || py < 0 || px >= P.w || py >= P.h) continue;
                s += std::abs(int(C.at(x, y)) - int(P.at(px, py))); ++n;
            }
        return n > C.w * C.h / (step * step) / 4 ? s / n : 1e9;
    };
    const int R4 = std::max(2, R / 4 + 1);
    double best = 1e18; int bx = 0, by = 0;
    for (int sy = -R4; sy <= R4; ++sy)
        for (int sx = -R4; sx <= R4; ++sx) {
            double s = sad(prev4, cur4, sx, sy, 1, std::max(2, R4)) + 0.02 * (std::abs(sx) + std::abs(sy));   // leve preferencia por quedarse quieto
            if (s < best) { best = s; bx = sx; by = sy; }
        }
    int cx = bx * 4, cy = by * 4; best = 1e18; int fx = cx, fy = cy;
    const int m1 = std::max(4, R + 2);
    for (int sy = cy - 4; sy <= cy + 4; ++sy)
        for (int sx = cx - 4; sx <= cx + 4; ++sx) {
            double s = sad(prev1, cur1, sx, sy, 2, m1);
            if (s < best) { best = s; fx = sx; fy = sy; }
        }
    mx = fx; my = fy;
}

// Analiza una escena: movimiento, camara y viabilidad del panorama. `shifts` (opcional) recibe el desplazamiento acumulado de cada frame.
inline ShotStats analyzeShot(const Clip& c, Shot s, std::vector<std::array<int, 2>>* offs = nullptr, int maxShift = 0) {
    ShotStats st;
    const int n = s.b - s.a;
    if (n < 2) { if (offs) offs->assign(size_t(std::max(n, 0)), {0, 0}); return st; }
    const int R = maxShift > 0 ? maxShift : std::max(6, c.w / 12);
    std::vector<Gray> g4, g1;
    for (int i = s.a; i < s.b; ++i) { g4.push_back(toGray(c.frames[size_t(i)], 4)); g1.push_back(toGray(c.frames[size_t(i)], 1)); }
    double mot = 0, mov = 0, cx = 0, pan = 0, resid = 0;
    std::vector<std::array<int, 2>> off(size_t(n), {0, 0});
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    int cum[2] = {0, 0};
    for (int i = 1; i < n; ++i) {
        const Image& A = c.frames[size_t(s.a + i - 1)]; const Image& B = c.frames[size_t(s.a + i)];
        mot += meanAbsDiff(A, B, 2); mov += changedFraction(A, B, 24, 2);
        int mx = 0, my = 0;
        estimateMotion(g4[size_t(i - 1)], g4[size_t(i)], g1[size_t(i - 1)], g1[size_t(i)], R, mx, my);
        pan += std::sqrt(double(mx) * mx + double(my) * my);
        // residuo tras compensar: compara B con A desplazada (en la zona comun)
        {
            size_t cnt = 0, bad = 0;
            for (int y = 2; y < B.h - 2; y += 3)
                for (int x = 2; x < B.w - 2; x += 3) {
                    int px = x - mx, py = y - my;
                    if (px < 0 || py < 0 || px >= A.w || py >= A.h) continue;
                    const uint8_t* p = B.at(x, y); const uint8_t* q = A.at(px, py);
                    int d = std::max({std::abs(int(p[0]) - q[0]), std::abs(int(p[1]) - q[1]), std::abs(int(p[2]) - q[2])});
                    if (d > 24) ++bad; ++cnt;
                }
            resid += cnt ? double(bad) / double(cnt) : 1.0;
        }
        cum[0] -= mx; cum[1] -= my;                 // posicion del frame i en el panorama = posicion anterior - movimiento del contenido
        off[size_t(i)] = {cum[0], cum[1]};
        minX = std::min(minX, cum[0]); maxX = std::max(maxX, cum[0]); minY = std::min(minY, cum[1]); maxY = std::max(maxY, cum[1]);
        cx += 0;
    }
    (void)cx;
    st.motion = mot / (n - 1); st.moving = mov / (n - 1); st.panPx = pan / (n - 1); st.residualAligned = resid / (n - 1);
    double cs = 0; for (int i = s.a; i < s.b; i += std::max(1, n / 6)) cs += complexityOf(c.frames[size_t(i)]);
    st.complexity = cs / double((n + std::max(1, n / 6) - 1) / std::max(1, n / 6));
    const double cw = c.w + (maxX - minX), ch = c.h + (maxY - minY);
    st.canvasArea = (cw * ch) / (double(c.w) * c.h);
    st.panOk = st.canvasArea <= 6.0;
    if (offs) *offs = off;
    return st;
}

// -------------------------------------------------- teselacion temporal ------

struct Cell { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; int area() const { return (x1 - x0) * (y1 - y0); } };

struct SatFrame {
    int W = 0, H = 0;
    std::vector<int32_t> r, g, b; std::vector<int64_t> q;
    size_t ix(int x, int y) const { return size_t(y) * (W + 1) + x; }
    void build(const Image& im) {
        W = im.w; H = im.h; size_t sz = size_t(W + 1) * (H + 1);
        r.assign(sz, 0); g.assign(sz, 0); b.assign(sz, 0); q.assign(sz, 0);
        for (int y = 0; y < H; ++y) {
            int32_t rr = 0, gg = 0, bb = 0; int64_t qq = 0;
            for (int x = 0; x < W; ++x) {
                const uint8_t* p = im.at(x, y);
                rr += p[0]; gg += p[1]; bb += p[2]; qq += int64_t(p[0]) * p[0] + int64_t(p[1]) * p[1] + int64_t(p[2]) * p[2];
                size_t i = ix(x + 1, y + 1), u = ix(x + 1, y);
                r[i] = r[u] + rr; g[i] = g[u] + gg; b[i] = b[u] + bb; q[i] = q[u] + qq;
            }
        }
    }
    void sums(int x0, int y0, int x1, int y1, double& sr, double& sg, double& sb, double& sq) const {
        size_t a = ix(x0, y0), bb = ix(x1, y0), c = ix(x0, y1), d = ix(x1, y1);
        sr = double(r[d]) - r[bb] - r[c] + r[a]; sg = double(g[d]) - g[bb] - g[c] + g[a]; sb = double(b[d]) - b[bb] - b[c] + b[a];
        sq = double(q[d]) - double(q[bb]) - double(q[c]) + double(q[a]);
    }
    std::array<uint8_t, 3> mean(int x0, int y0, int x1, int y1) const {
        double sr, sg, sb, sq; sums(x0, y0, x1, y1, sr, sg, sb, sq);
        double n = double(x1 - x0) * double(y1 - y0);
        if (n <= 0) return {0, 0, 0};
        return {uint8_t(std::clamp(sr / n + 0.5, 0.0, 255.0)), uint8_t(std::clamp(sg / n + 0.5, 0.0, 255.0)), uint8_t(std::clamp(sb / n + 0.5, 0.0, 255.0))};
    }
};

// Reparte el raster en K rectangulos que minimizan el error cuadratico acumulado de TODOS los frames dados (cada rectangulo se pinta
// con su color medio en cada frame). Mejor-primero: siempre se parte la celda cuyo corte mas reduce el error. Con esto las teselas se
// concentran donde hay detalle o movimiento a lo largo de la escena (y quedan grandes donde nunca pasa nada).
inline std::vector<Cell> temporalCells(const std::vector<const Image*>& frames, int K, int minSize = 2, Progress* prog = nullptr) {
    std::vector<Cell> out;
    if (frames.empty() || K < 1) return out;
    const int W = frames[0]->w, H = frames[0]->h;
    std::vector<SatFrame> sat(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) sat[i].build(*frames[i]);
    auto sse = [&](int x0, int y0, int x1, int y1) {
        double tot = 0; const double n = double(x1 - x0) * double(y1 - y0);
        if (n <= 0) return 0.0;
        for (auto& s : sat) { double sr, sg, sb, sq; s.sums(x0, y0, x1, y1, sr, sg, sb, sq); tot += std::max(0.0, sq - (sr * sr + sg * sg + sb * sb) / n); }
        return tot;
    };
    struct Node { Cell c; double gain = 0; bool vertical = false; int at = 0; double base = 0; };
    auto best = [&](Cell c) {
        Node nd; nd.c = c; nd.base = sse(c.x0, c.y0, c.x1, c.y1);
        const int w = c.x1 - c.x0, h = c.y1 - c.y0;
        const int sx = std::max(1, w / 28), sy = std::max(1, h / 28);
        for (int x = c.x0 + minSize; x <= c.x1 - minSize; x += sx) {
            double g = nd.base - sse(c.x0, c.y0, x, c.y1) - sse(x, c.y0, c.x1, c.y1);
            if (g > nd.gain) { nd.gain = g; nd.vertical = true; nd.at = x; }
        }
        for (int y = c.y0 + minSize; y <= c.y1 - minSize; y += sy) {
            double g = nd.base - sse(c.x0, c.y0, c.x1, y) - sse(c.x0, y, c.x1, c.y1);
            if (g > nd.gain) { nd.gain = g; nd.vertical = false; nd.at = y; }
        }
        return nd;
    };
    auto cmp = [](const Node& a, const Node& b) { return a.gain < b.gain; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> pq(cmp);
    pq.push(best({0, 0, W, H}));
    std::vector<Cell> done;
    int leaves = 1;
    while (!pq.empty() && leaves < K) {
        Node n = pq.top(); pq.pop();
        if (n.gain <= 1e-6) { done.push_back(n.c); continue; }          // nada que ganar: queda como hoja definitiva
        Cell a = n.c, b = n.c;
        if (n.vertical) { a.x1 = n.at; b.x0 = n.at; } else { a.y1 = n.at; b.y0 = n.at; }
        pq.push(best(a)); pq.push(best(b)); ++leaves;
        if (prog && (leaves & 31) == 0) gdx::report(prog, std::min(1.f, float(leaves) / float(K)));
        if (gdx::isCancelled(prog)) break;
    }
    while (!pq.empty()) { done.push_back(pq.top().c); pq.pop(); }
    return done;
}

// color medio de cada celda en un frame
inline std::vector<std::array<uint8_t, 3>> cellMeans(const Image& im, const std::vector<Cell>& cells) {
    SatFrame s; s.build(im);
    std::vector<std::array<uint8_t, 3>> o(cells.size());
    for (size_t i = 0; i < cells.size(); ++i) o[i] = s.mean(cells[i].x0, cells[i].y0, cells[i].x1, cells[i].y1);
    return o;
}

inline void fillCells(Image& im, const std::vector<Cell>& cells, const std::vector<std::array<uint8_t, 3>>& col) {
    for (size_t i = 0; i < cells.size(); ++i)
        for (int y = cells[i].y0; y < cells[i].y1; ++y) {
            uint8_t* p = im.at(cells[i].x0, y);
            for (int x = cells[i].x0; x < cells[i].x1; ++x, p += 4) { p[0] = col[i][0]; p[1] = col[i][1]; p[2] = col[i][2]; p[3] = 255; }
        }
}

} // namespace vx
