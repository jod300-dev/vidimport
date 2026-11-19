// ============================================================================
//  vx :: pintado de frames y residuos (reutiliza los pintores del motor: BSP, hibrido con rectangulos girados)
// ============================================================================
#pragma once
#include "analysis.hpp"
#include "engine.hpp"

namespace vx {

inline gdx::Options paintOptions(const Params& P, int budget, int W, int H) {
    gdx::Options o;
    o.budget = std::max(1, budget);
    o.maxRaster = std::max(W, H);
    o.alphaThreshold = std::clamp(P.alphaThr, 1, 255);
    o.quality = std::clamp(P.quality, 0, 3);
    o.grayscale = false;                 // los frames ya vienen en gris si se pidio
    o.mainChannels = false; o.colorChannels = false; o.lightSpecialChannels = true; o.maxChannels = 0;
    return o;
}

// lleva las formas al raster W x H (el motor puede devolver un raster algo distinto)
inline void rescaleTo(Result& r, int W, int H) {
    if (r.rasterW <= 0 || r.rasterH <= 0 || (r.rasterW == W && r.rasterH == H)) { r.rasterW = W; r.rasterH = H; return; }
    const float fx = float(W) / r.rasterW, fy = float(H) / r.rasterH;
    for (Shape& s : r.shapes) { s.cx *= fx; s.cy *= fy; s.w *= fx; s.h *= fy; }
    r.rasterW = W; r.rasterH = H;
}

// frame completo -> formas (BSP; con calidad alta y pocos objetos, BSP + rectangulos girados)
inline Result paintFull(const Image& frame, int budget, const Params& P) {
    gdx::Options o = paintOptions(P, budget, frame.w, frame.h);
    const bool hybrid = P.quality >= 2 && budget <= 2500;
    Result r = gdx::convert(frame, hybrid ? gdx::Method::Hybrid : gdx::Method::BSP, o, nullptr);
    rescaleTo(r, frame.w, frame.h);
    return r;
}

inline Image blankCanvas(int W, int H) { Image c; c.w = W; c.h = H; c.rgba.assign(size_t(W) * H * 4, 0); return c; }

// pinta las formas (en orden de z) sobre el lienzo
inline void paintOnto(Image& canvas, const std::vector<Shape>& shapes) {
    if (shapes.empty()) return;
    Image l = gdx::renderShapes(shapes, canvas.w, canvas.h);
    for (size_t i = 0; i < size_t(canvas.w) * canvas.h; ++i)
        if (l.rgba[i * 4 + 3]) { canvas.rgba[i * 4] = l.rgba[i * 4]; canvas.rgba[i * 4 + 1] = l.rgba[i * 4 + 1]; canvas.rgba[i * 4 + 2] = l.rgba[i * 4 + 2]; canvas.rgba[i * 4 + 3] = 255; }
}

// Residuo: formas que, pintadas ENCIMA de `canvas`, acercan el resultado a `target`. Solo cubre los pixeles que difieren mas de `tol`
// (con 1 px de margen): el BSP trata lo demas como transparente y por eso solo gasta objetos donde hay error.
inline std::vector<Shape> residualShapes(const Image& target, const Image& canvas, int tol, int budget, const Params& P, double* dirtyFrac = nullptr, const std::vector<char>* allow = nullptr) {
    const int W = target.w, H = target.h;
    std::vector<char> dirty(size_t(W) * H, 0);
    size_t cnt = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const uint8_t* p = target.at(x, y); const uint8_t* q = canvas.at(x, y);
            int d = std::max({std::abs(int(p[0]) - q[0]), std::abs(int(p[1]) - q[1]), std::abs(int(p[2]) - q[2])});
            if (canvas.rgba[(size_t(y) * W + x) * 4 + 3] == 0) d = 255;           // lo no pintado cuenta como error
            if (d > tol && (!allow || (*allow)[size_t(y) * W + x])) { dirty[size_t(y) * W + x] = 1; ++cnt; }
        }
    if (dirtyFrac) *dirtyFrac = double(cnt) / (double(W) * H);
    if (cnt < size_t(std::max(6, W * H / 1500)) || budget < 1) return {};
    Image m; m.w = W; m.h = H; m.rgba.assign(size_t(W) * H * 4, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            bool on = false;
            for (int j = -1; j <= 1 && !on; ++j)
                for (int i = -1; i <= 1; ++i) { int xx = x + i, yy = y + j; if (xx >= 0 && yy >= 0 && xx < W && yy < H && dirty[size_t(yy) * W + xx]) { on = true; break; } }
            if (on) { const uint8_t* p = target.at(x, y); uint8_t* o = m.at(x, y); o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = 255; }
        }
    gdx::Options o = paintOptions(P, budget, W, H);
    Result r = gdx::convert(m, gdx::Method::BSP, o, nullptr);
    rescaleTo(r, W, H);
    return std::move(r.shapes);
}

} // namespace vx
