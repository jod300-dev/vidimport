// Simulador (modelo) de GD para el mod de video: parsea las cadenas generadas y ejecuta Spawn / Toggle / Alpha / Color (con fundido) /
// Pulse / Static Camera en el tiempo; renderiza lo que vería la camara en un instante. Sirve para comprobar la logica de los
// metodos contra los frames originales (NO sustituye probar en GD real: modela lo que el mod ESPERA de cada trigger).
#pragma once
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include "core.hpp"
#include "analysis.hpp"
namespace vsim {
using gdx::Image;
struct Obj {
    std::map<int, std::string> kv; std::vector<int> groups; int id = 0; double x = 0, y = 0; long order = 0;
    bool active = true; double alpha = 1.0;
    double num(int k, double d = 0) const { auto it = kv.find(k); return it == kv.end() ? d : std::stod(it->second); }
    bool has(int k) const { return kv.count(k) > 0; }
};
inline void hsv2rgb(double h, double s, double v, double& r, double& g, double& b) {
    h = std::fmod(h, 360.0); if (h < 0) h += 360.0;
    double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c, rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; } else if (h < 120) { rr = x; gg = c; } else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; } else if (h < 300) { rr = x; bb = c; } else { rr = c; bb = x; }
    r = rr + m; g = gg + m; b = bb + m;
}
struct Sim {
    std::vector<Obj> objs;
    std::map<int, std::vector<int>> byGroup;      // grupo -> objetos que pertenecen
    struct Fade { double t0, t1; double from[3], to[3]; };
    std::map<int, std::array<double, 3>> chanBase; std::map<int, Fade> chanFade;   // canal -> color y fundido en curso
    std::map<int, std::array<double,3>> pulseCol; // objeto -> color de pulso (hold)
    std::map<int, double> pulseUntil;
    double camX = 0, camY = 0; bool camSet = false;
    struct CamFade { double t0, t1, fx, fy, tx, ty; bool on = false; } cam;
    struct Ev { double t; long seq; int obj; bool operator<(const Ev& o) const { return t > o.t || (t == o.t && seq > o.seq); } };
    std::priority_queue<Ev> q; long seq = 0; double now = 0;
    long fired = 0;
    // alpha animado por objeto
    struct AFade { double t0, t1, from, to; }; std::map<int, AFade> alphaFade;
    std::map<int, std::vector<int>> spawnIn;      // grupo -> triggers (spawn-triggered) que contiene

    void load(const std::vector<std::string>& chunks) {
        long ord = 0;
        for (auto& c : chunks) {
            std::stringstream ss(c); std::string item;
            while (std::getline(ss, item, ';')) {
                Obj o; std::stringstream is(item); std::string a, b;
                while (std::getline(is, a, ',') && std::getline(is, b, ',')) o.kv[std::stoi(a)] = b;
                if (!o.has(1)) continue;
                o.id = int(o.num(1)); o.x = o.num(2); o.y = o.num(3); o.order = ord++;
                if (o.has(57)) { std::stringstream gs(o.kv[57]); std::string g; while (std::getline(gs, g, '.')) o.groups.push_back(std::stoi(g)); }
                objs.push_back(std::move(o));
            }
        }
        for (size_t i = 0; i < objs.size(); ++i) {
            for (int g : objs[i].groups) byGroup[g].push_back(int(i));
            if (objs[i].has(62)) for (int g : objs[i].groups) spawnIn[g].push_back(int(i));
        }
    }
    void schedule(int obj, double t) { q.push({t, seq++, obj}); }
    void startPositionTriggers() { for (size_t i = 0; i < objs.size(); ++i) { auto& o = objs[i]; if ((o.id == 1268) && !o.has(62)) schedule(int(i), 0); } }
    void fireGroup(int g, double t) { auto it = spawnIn.find(g); if (it == spawnIn.end()) return; for (int i : it->second) schedule(i, t + objs[size_t(i)].num(63, 0)); }
    double colorAt(const std::array<double,3>& base, int ch, double t, double out[3]) { (void)base; (void)ch; (void)t; (void)out; return 0; }
    void getChan(int ch, double t, double out[3]) {
        std::array<double,3> b = chanBase.count(ch) ? chanBase[ch] : std::array<double,3>{255, 255, 255};
        auto f = chanFade.find(ch);
        if (f != chanFade.end()) {
            const Fade& F = f->second;
            double u = F.t1 <= F.t0 ? 1.0 : std::clamp((t - F.t0) / (F.t1 - F.t0), 0.0, 1.0);
            for (int k = 0; k < 3; ++k) out[k] = F.from[k] + (F.to[k] - F.from[k]) * u;
            return;
        }
        for (int k = 0; k < 3; ++k) out[k] = b[size_t(k)];
    }
    void execute(int oi, double t) {
        Obj& o = objs[size_t(oi)]; ++fired;
        switch (o.id) {
            case 1268: { int tg = int(o.num(51)); fireGroup(tg, t); break; }
            case 1049: { bool on = o.num(56) != 0; auto it = byGroup.find(int(o.num(51))); if (it != byGroup.end()) for (int i : it->second) objs[size_t(i)].active = on; break; }
            case 1007: {
                double to = o.num(35, 1), d = o.num(10, 0);
                auto it = byGroup.find(int(o.num(51))); if (it == byGroup.end()) break;
                for (int i : it->second) { double cur = curAlpha(i, t); if (d <= 0) { objs[size_t(i)].alpha = to; alphaFade.erase(i); } else alphaFade[i] = {t, t + d, cur, to}; }
                break; }
            case 899: {
                int ch = int(o.num(23)); double d = o.num(10, 0); double cur[3]; getChan(ch, t, cur);
                Fade F; F.t0 = t; F.t1 = t + d; for (int k = 0; k < 3; ++k) { F.from[k] = cur[k]; F.to[k] = o.num(7 + k); }
                if (d <= 0) { chanFade.erase(ch); chanBase[ch] = {F.to[0], F.to[1], F.to[2]}; } else chanFade[ch] = F;
                break; }
            case 1006: {
                auto it = byGroup.find(int(o.num(51))); if (it == byGroup.end()) break;
                for (int i : it->second) { pulseCol[i] = {o.num(7), o.num(8), o.num(9)}; pulseUntil[i] = t + o.num(45) + o.num(46) + o.num(47); }
                break; }
            case 2062: {
                auto it = byGroup.find(int(o.num(71))); if (it == byGroup.end() || it->second.empty()) break;
                const Obj& a = objs[size_t(it->second[0])]; double d = o.num(10, 0);
                if (d <= 0) { camX = a.x; camY = a.y; cam.on = false; camSet = true; }
                else { cam = {t, t + d, camPos(t, 0), camPos(t, 1), a.x, a.y, true}; }
                break; }
        }
    }
    double camPos(double t, int axis) {
        if (!cam.on) return axis == 0 ? camX : camY;
        double u = std::clamp((t - cam.t0) / std::max(1e-9, cam.t1 - cam.t0), 0.0, 1.0);
        return axis == 0 ? cam.fx + (cam.tx - cam.fx) * u : cam.fy + (cam.ty - cam.fy) * u;
    }
    double curAlpha(int i, double t) { auto f = alphaFade.find(i); if (f == alphaFade.end()) return objs[size_t(i)].alpha; auto& F = f->second; double u = std::clamp((t - F.t0) / std::max(1e-9, F.t1 - F.t0), 0.0, 1.0); return F.from + (F.to - F.from) * u; }
    void advance(double t) {       // ejecuta todo lo programado hasta t (inclusive)
        while (!q.empty() && q.top().t <= t + 1e-9) { Ev e = q.top(); q.pop(); now = e.t; execute(e.obj, e.t); }
        now = t;
    }
    // render de la camara en el instante t: ventana de (viewW x viewH) unidades centrada en la camara, a escala `ppu` pixeles por unidad
    Image render(double t, double viewW, double viewH, double ppu, std::vector<int>* visibleObjs = nullptr) {
        Image im; im.w = int(viewW * ppu); im.h = int(viewH * ppu); im.rgba.assign(size_t(im.w) * im.h * 4, 0);
        double cx = camPos(t, 0), cy = camPos(t, 1);
        std::vector<int> ord;
        for (size_t i = 0; i < objs.size(); ++i) {
            const Obj& o = objs[i]; if (o.id != 3092 || !o.active) continue;
            double w = o.num(128, 1), h = o.num(129, 1);
            if (o.x + w / 2 < cx - viewW / 2 || o.x - w / 2 > cx + viewW / 2 || o.y + h / 2 < cy - viewH / 2 || o.y - h / 2 > cy + viewH / 2) continue;
            ord.push_back(int(i));
        }
        if (visibleObjs) *visibleObjs = ord;
        std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) { double za = objs[size_t(a)].num(25), zb = objs[size_t(b)].num(25); return za < zb; });
        for (int i : ord) {
            const Obj& o = objs[size_t(i)];
            double r, g, b, ch = o.num(21, 1010); double base[3];
            if (o.has(43) && o.num(41) == 1) {
                std::vector<double> p; { std::stringstream hs(o.kv.at(43)); std::string tk; while (std::getline(hs, tk, 'a')) p.push_back(std::stod(tk)); }
                double h = p[0], s = p[1], v = p[2]; hsv2rgb(h, s, v, r, g, b); base[0] = r * 255; base[1] = g * 255; base[2] = b * 255;
            } else { getChan(int(ch), t, base); }
            auto pu = pulseCol.find(i);
            if (pu != pulseCol.end() && t <= pulseUntil[i]) { base[0] = pu->second[0]; base[1] = pu->second[1]; base[2] = pu->second[2]; }
            double a = curAlpha(i, t);
            double w = o.num(128, 1), h = o.num(129, 1);
            int x0 = int(std::floor((o.x - w / 2 - (cx - viewW / 2)) * ppu + 0.5)), x1 = int(std::floor((o.x + w / 2 - (cx - viewW / 2)) * ppu + 0.5));
            int y0 = int(std::floor(((cy + viewH / 2) - (o.y + h / 2)) * ppu + 0.5)), y1 = int(std::floor(((cy + viewH / 2) - (o.y - h / 2)) * ppu + 0.5));
            for (int y = std::max(0, y0); y < std::min(im.h, y1); ++y) for (int x = std::max(0, x0); x < std::min(im.w, x1); ++x) {
                uint8_t* p = im.at(x, y);
                for (int k = 0; k < 3; ++k) p[k] = uint8_t(std::clamp(p[k] * (1 - a) + base[k] * a + 0.5, 0.0, 255.0)); p[3] = 255;
            }
        }
        return im;
    }
};
}
