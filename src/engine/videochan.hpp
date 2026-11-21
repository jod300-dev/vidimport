// ============================================================================
//  gdx :: colores en CANALES (sin HSV) para el video
//  Sin dependencias de Geode. Reciclado de chanframes.hpp / layered.hpp del GIF Importer.
//
//   * UNA paleta comun a TODOS los frames del video (corte mediano + Lloyd, ponderada por area)
//   * cada objeto lleva solo  21,<canal>  (sin las claves HSV 41/43): cadena mas corta y GD carga mas rapido
//   * los canales son  2 .. 1 + K : el canal 1 NUNCA se usa y siempre se empieza por el 2
//   * el color de cada canal lo fija un Color trigger (id 899) AL INICIO del nivel (uno por canal, una sola vez):
//     la animacion en si sigue usando solo Toggle + Spawn
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "gdexport.hpp"

namespace gdx {

constexpr int kVideoFirstChannel = 2;                         // el canal 1 nunca se usa
constexpr int kVideoMaxPalette = 900;

namespace lay {
inline uint32_t colKey(uint8_t r, uint8_t g, uint8_t b) { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }
inline double colDist(const double* a, const double* b) {
    double dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return 2.0 * dr * dr + 4.0 * dg * dg + 3.0 * db * db;
}

inline std::vector<std::array<uint8_t, 3>> buildPalette(const std::map<uint32_t, double>& hist, int K) {
    struct C { double c[3]; double w; };
    std::vector<C> cols; cols.reserve(hist.size());
    for (auto& [k, w] : hist) cols.push_back({{double(k >> 16 & 255), double(k >> 8 & 255), double(k & 255)}, w});
    std::vector<std::vector<int>> boxes(1);
    for (int i = 0; i < int(cols.size()); ++i) boxes[0].push_back(i);
    auto range = [&](const std::vector<int>& b, int& axis) {
        double lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        for (int i : b) for (int c = 0; c < 3; ++c) { lo[c] = std::min(lo[c], cols[size_t(i)].c[c]); hi[c] = std::max(hi[c], cols[size_t(i)].c[c]); }
        double best = -1; axis = 0;
        for (int c = 0; c < 3; ++c) { double r = (hi[c] - lo[c]) * (c == 1 ? 1.4 : c == 2 ? 1.1 : 1.0); if (r > best) { best = r; axis = c; } }
        return best;
    };
    while (int(boxes.size()) < K) {
        int pick = -1, pa = 0; double pbest = 0;
        for (int bi = 0; bi < int(boxes.size()); ++bi) {
            if (boxes[size_t(bi)].size() < 2) continue;
            int ax; double r = range(boxes[size_t(bi)], ax);
            double w = 0; for (int i : boxes[size_t(bi)]) w += cols[size_t(i)].w;
            double score = r * std::sqrt(w + 1.0);
            if (r > 0 && score > pbest) { pbest = score; pick = bi; pa = ax; }
        }
        if (pick < 0) break;
        auto b = std::move(boxes[size_t(pick)]);
        std::sort(b.begin(), b.end(), [&](int x, int y) { return cols[size_t(x)].c[pa] < cols[size_t(y)].c[pa]; });
        double tot = 0; for (int i : b) tot += cols[size_t(i)].w;
        double acc = 0; size_t cut = 1;
        for (size_t i = 0; i < b.size(); ++i) { acc += cols[size_t(b[i])].w; if (acc >= tot * 0.5) { cut = std::clamp<size_t>(i + 1, 1, b.size() - 1); break; } }
        boxes[size_t(pick)] = std::vector<int>(b.begin(), b.begin() + long(cut));
        boxes.push_back(std::vector<int>(b.begin() + long(cut), b.end()));
    }
    std::vector<std::array<double, 3>> cen(boxes.size());
    auto recenter = [&](const std::vector<std::vector<int>>& bx) {
        for (size_t bi = 0; bi < bx.size(); ++bi) {
            double acc[3] = {0, 0, 0}, w = 0;
            for (int i : bx[bi]) { for (int c = 0; c < 3; ++c) acc[c] += cols[size_t(i)].c[c] * cols[size_t(i)].w; w += cols[size_t(i)].w; }
            for (int c = 0; c < 3; ++c) cen[bi][size_t(c)] = w > 0 ? acc[c] / w : 0;
        }
    };
    recenter(boxes);
    for (int it = 0; it < 5; ++it) {                                          // Lloyd
        std::vector<std::vector<int>> nb(boxes.size());
        for (int i = 0; i < int(cols.size()); ++i) {
            size_t bj = 0; double bd = 1e300;
            for (size_t j = 0; j < cen.size(); ++j) { double d = colDist(cols[size_t(i)].c, cen[j].data()); if (d < bd) { bd = d; bj = j; } }
            nb[bj].push_back(i);
        }
        std::vector<std::vector<int>> keep; std::vector<std::array<double, 3>> old = cen;
        for (size_t j = 0; j < nb.size(); ++j) if (!nb[j].empty()) keep.push_back(std::move(nb[j]));
        boxes = std::move(keep); cen.assign(boxes.size(), {0, 0, 0});
        recenter(boxes);
    }
    std::vector<std::array<uint8_t, 3>> out;
    for (auto& c : cen) out.push_back({uint8_t(std::clamp(c[0] + 0.5, 0.0, 255.0)), uint8_t(std::clamp(c[1] + 0.5, 0.0, 255.0)), uint8_t(std::clamp(c[2] + 0.5, 0.0, 255.0))});
    return out;
}

} // namespace lay

// Cuantiza los colores de todos los grupos de frames a <= K colores y los marca como `colored`. Devuelve la paleta USADA.
// El histograma se hace a 5 bits por canal (<= 32768 colores): rapido aunque haya 450000 formas; la asignacion final es exacta.
inline std::vector<std::array<uint8_t, 3>> quantizeVideo(std::vector<Result>& results, int K) {
    K = std::clamp(K, 2, kVideoMaxPalette);
    std::map<uint32_t, double> hist;
    for (auto& r : results)
        for (auto& sh : r.shapes)
            hist[lay::colKey(uint8_t((sh.r & 0xF8) | 4), uint8_t((sh.g & 0xF8) | 4), uint8_t((sh.b & 0xF8) | 4))] += double(sh.w) * sh.h + 1.0;
    std::vector<std::array<uint8_t, 3>> out;
    if (hist.empty()) return out;
    const auto pal = lay::buildPalette(hist, K);
    std::unordered_map<uint32_t, int> cache;
    std::vector<char> used(pal.size(), 0);
    auto nearest = [&](uint8_t r, uint8_t g, uint8_t b) {
        const uint32_t k = lay::colKey(r, g, b);
        auto it = cache.find(k);
        if (it != cache.end()) return it->second;
        const double c[3] = {double(r), double(g), double(b)};
        int bi = 0; double bd = 1e300;
        for (size_t j = 0; j < pal.size(); ++j) {
            const double pc[3] = {double(pal[j][0]), double(pal[j][1]), double(pal[j][2])};
            const double d = lay::colDist(c, pc);
            if (d < bd) { bd = d; bi = int(j); }
        }
        cache[k] = bi;
        return bi;
    };
    for (auto& r : results) {
        for (auto& sh : r.shapes) {
            const int bi = nearest(sh.r, sh.g, sh.b);
            sh.r = pal[size_t(bi)][0]; sh.g = pal[size_t(bi)][1]; sh.b = pal[size_t(bi)][2];
            used[size_t(bi)] = 1;
        }
        r.colored = true;
    }
    for (size_t j = 0; j < pal.size(); ++j) if (used[j]) out.push_back(pal[j]);
    return out;
}

namespace detail {
// Posicion de los Color triggers: cuadricula compacta (paso 30 u, 20 columnas) a la izquierda del inicio y encima de los de arranque.
inline void colorTriggerPos(int i, double trigX, double sy, double& cx, double& cy) {
    cx = trigX - 30.0 * double(1 + i % 20);
    cy = sy + 90.0 + 30.0 * double(i / 20);
}
} // namespace detail

// Un Color trigger (id 899) por canal de la paleta (se ejecutan al empezar el nivel).
inline void appendColorTriggers(std::string& out, const std::vector<std::array<uint8_t, 3>>& palette, int firstChannel, double trigX, double sy) {
    for (size_t i = 0; i < palette.size(); ++i) {
        double cx, cy; detail::colorTriggerPos(int(i), trigX, sy, cx, cy);
        out += "1,899,2,"; detail::appendNum(out, cx, 2); out += ",3,"; detail::appendNum(out, cy, 2); out += ",36,1";
        out += ",7,"; out += std::to_string(palette[i][0]);
        out += ",8,"; out += std::to_string(palette[i][1]);
        out += ",9,"; out += std::to_string(palette[i][2]);
        out += ",10,0,35,1,17,0,23,"; out += std::to_string(firstChannel + int(i));
        out += ';';
    }
}

} // namespace gdx
