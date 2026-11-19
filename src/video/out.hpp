// ============================================================================
//  vx :: salida hacia GD. Reune formas y triggers en "trozos" listos para createObjectsFromString.
//
//  Reglas aprendidas del GIF Importer (y que se conservan):
//    * el z-order (clave 25) se emite SIEMPRE y nunca es 0 (GD trata el 0 como "z por defecto" y lo dibuja encima).
//    * los triggers van PRIMERO en la salida: si GD cortase la cadena por el limite del editor, se perderian formas, no la animacion.
//    * cada paso de la animacion es IDEMPOTENTE (fija un estado absoluto) y puede lanzarse dos veces (autocorreccion ante lag).
//
//  Triggers usados (ids de objeto):  Spawn 1268, Toggle 1049, Alpha 1007, Color 899, Static Camera 2062, Pulse 1006.
// ============================================================================
#pragma once
#include <cmath>
#include <map>
#include "gdexport.hpp"
#include "types.hpp"

namespace vx {

constexpr int kObjSquare = 3092;
constexpr int kTrigSpawn = 1268, kTrigToggle = 1049, kTrigAlpha = 1007, kTrigColor = 899, kTrigCamera = 2062, kTrigPulse = 1006;

// --------------------------------------------------------------- trozos ----

struct ChunkBuf {
    std::vector<std::string> done;
    std::string cur;
    size_t n = 0, per = 1500;
    std::string& begin() { if (n) cur += ';'; return cur; }          // devuelve la cadena para escribir UN objeto
    void end() { ++n; if (n >= per) flush(); }
    void flush() { if (n) { done.push_back(std::move(cur)); cur.clear(); n = 0; } }
};

// ---------------------------------------------------------------- layout ----
// Reparte rectangulos (cuadros de video) en estantes dejando HUECOS grandes: la camara de GD ve hasta ~400 u a cada lado del
// centro (pantallas anchas), asi que entre dos cuadros hay >= 2 x 420 u y nunca se cuela un cuadro vecino en pantalla.
struct Shelf {
    double x0 = 0, y0 = 0, maxRow = 26000.0, marginX = 420.0, marginY = 260.0;
    double cursorX = 0, rowY = 0, rowH = 0;
    struct Slot { double cx = 0, cy = 0; };
    Slot alloc(double w, double h) {
        const double mw = w + 2 * marginX, mh = h + 2 * marginY;
        if (cursorX > 0 && cursorX + mw > maxRow) { cursorX = 0; rowY += rowH; rowH = 0; }
        Slot s{x0 + cursorX + mw * 0.5, y0 + rowY + mh * 0.5};
        cursorX += mw; rowH = std::max(rowH, mh);
        return s;
    }
};

// -------------------------------------------------------- formato de forma --

struct ShapeFmt {
    double cx = 0, cy = 0;             // centro del cuadro (unidades GD) = centro del raster
    int W = 1, H = 1;                  // raster de las formas
    double ux = 1, uy = 1;             // unidades GD por pixel del raster
    int zBase = 0;                     // z = zBase + shape.z, nunca < 1
    std::string groups;                // "12" o "12.13" (vacio = sin grupo)
    int fixedChannel = -1;             // >= 2: todas las formas usan este canal (sin HSV)
    const std::map<uint32_t, int>* palette = nullptr;   // color -> canal (sin HSV). Si no hay, HSV sobre colorChannel
};

class Out {
public:
    explicit Out(const Params& p) : P(p), nextGroup(std::max(1, p.firstGroup)) {
        parkX = p.originX - 60.0; parkY = p.originY;
    }
    const Params& P;
    ChunkBuf trig, obj;
    int nextGroup;
    bool overflow = false;
    size_t nShape = 0, nTrig = 0;

    struct Event { double t; int group; bool reassert; double dt; };
    std::vector<Event> events;

    int groupsUsed(int first) const { return nextGroup - first; }
    int group() {
        if (nextGroup > kGroupMax) { overflow = true; return kGroupMax; }
        return nextGroup++;
    }
    static std::string gs(int a, int b = 0, int c = 0) {
        std::string s = std::to_string(a);
        if (b > 0) { s += '.'; s += std::to_string(b); }
        if (c > 0) { s += '.'; s += std::to_string(c); }
        return s;
    }

    // ---------------------------------------------------------- formas ----
    void shape(const Shape& s, const ShapeFmt& f) {
        std::string& o = obj.begin();
        const double x = f.cx + (s.cx - f.W * 0.5) * f.ux;
        const double y = f.cy - (s.cy - f.H * 0.5) * f.uy;
        const bool rotated = std::abs(std::fmod(s.rot, 180.0f)) > 0.01f;
        const double bl = rotated ? 0.0 : P.bleed;
        o += "1,"; o += std::to_string(kObjSquare);
        o += ",2,"; gdx::detail::appendNum(o, x, 3);
        o += ",3,"; gdx::detail::appendNum(o, y, 3);
        int ch = -1;
        if (f.fixedChannel >= 2) ch = f.fixedChannel;
        else if (f.palette) { auto it = f.palette->find(gdx::ChannelMap::key(s.r, s.g, s.b)); if (it != f.palette->end()) ch = it->second; }
        if (ch >= 2) { o += ",21,"; o += std::to_string(ch); }
        else {
            o += ",21,"; o += std::to_string(P.colorChannel);
            o += ",41,1,43,"; gdx::appendGdHsv(o, s.r, s.g, s.b);
        }
        o += ",25,"; o += std::to_string(std::max(f.zBase + int(s.z), 0) + 1);
        if (!f.groups.empty()) { o += ",57,"; o += f.groups; }
        o += ",128,"; gdx::detail::appendNum(o, s.w * f.ux + bl, 3);
        o += ",129,"; gdx::detail::appendNum(o, s.h * f.uy + bl, 3);
        if (rotated) {
            double rot = P.flipRotation ? -s.rot : s.rot;
            rot = std::fmod(rot, 360.0);
            if (rot > 180) rot -= 360; else if (rot <= -180) rot += 360;
            o += ",6,"; gdx::detail::appendNum(o, rot, 2);
        }
        obj.end(); ++nShape;
    }
    void shapes(const std::vector<Shape>& v, const ShapeFmt& f) { for (const Shape& s : v) shape(s, f); }

    // --------------------------------------------------------- triggers ----
    // own = grupo(s) del trigger ("" = trigger de posicion: se activa al llegar a x). Los demas son "spawn triggered" en ese grupo.
    void park(double& x, double& y) {
        x = parkX - 30.0 * double(parkN % 40);
        y = parkY + 30.0 * double(parkN / 40);
        ++parkN;
    }
    void spawn(const std::string& own, int target, double delay) {
        double x, y; park(x, y); spawnAt(x, y, own, target, delay);
    }
    void spawnAt(double x, double y, const std::string& own, int target, double delay) {
        std::string& o = head(kTrigSpawn, x, y);
        o += ",51,"; o += std::to_string(target);
        if (delay > 0) { o += ",63,"; gdx::detail::appendNum(o, delay, 4); }
        tail(o, own);
    }
    void toggle(const std::string& own, int target, bool on) {
        double x, y; park(x, y);
        std::string& o = head(kTrigToggle, x, y);
        o += ",51,"; o += std::to_string(target);
        o += ",56,"; o += on ? '1' : '0';
        tail(o, own);
    }
    void alpha(const std::string& own, int target, int opacity01, double fade) {
        double x, y; park(x, y);
        std::string& o = head(kTrigAlpha, x, y);
        o += ",51,"; o += std::to_string(target);
        o += ",35,"; o += std::to_string(opacity01);
        o += ",10,"; gdx::detail::appendNum(o, fade, 4);
        tail(o, own);
    }
    void color(const std::string& own, int channel, const std::array<uint8_t, 3>& c, double fade) {
        double x, y; park(x, y);
        std::string& o = head(kTrigColor, x, y);
        o += ",7,"; o += std::to_string(c[0]);
        o += ",8,"; o += std::to_string(c[1]);
        o += ",9,"; o += std::to_string(c[2]);
        o += ",10,"; gdx::detail::appendNum(o, fade, 4);
        o += ",35,1,17,0,23,"; o += std::to_string(channel);
        tail(o, own);
    }
    // Camara estatica. El trigger esta en la posicion (x, y) y es su PROPIO ancla: pertenece al grupo `anchor` y apunta a el, asi que la
    // camara se centra en (x, y) sin hacer falta ningun objeto extra.
    void camera(const std::string& own, double x, double y, int anchor, double fade) {
        std::string& o = head(kTrigCamera, x, y);
        o += ",71,"; o += std::to_string(anchor);
        o += ",10,"; gdx::detail::appendNum(o, fade, 4);
        tail(o, own);
    }
    void pulseGroup(const std::string& own, int targetGroup, const std::array<uint8_t, 3>& c, double hold) {
        double x, y; park(x, y);
        std::string& o = head(kTrigPulse, x, y);
        o += ",7,"; o += std::to_string(c[0]);
        o += ",8,"; o += std::to_string(c[1]);
        o += ",9,"; o += std::to_string(c[2]);
        o += ",45,0,46,"; gdx::detail::appendNum(o, hold, 3);
        o += ",47,0,48,0,52,1,51,"; o += std::to_string(targetGroup);
        o += ",86,1";
        tail(o, own);
    }

    // ---------------------------------------------------------- eventos ----
    // Un evento = "en el instante t (desde el inicio del bucle) lanza el grupo `group`".
    void event(double t, int group, bool reassert = true, double dt = 0.04) { events.push_back({t, group, reassert, dt}); }

    // Monta el bucle: segmentos de <= 100 Spawn, un maestro que los lanza y que se relanza a si mismo, y los 2 triggers de arranque.
    // Devuelve el numero de grupos usados por la estructura.
    void finish(double total) {
        std::vector<Event> ev;
        for (const Event& e : events) {
            ev.push_back(e);
            if (e.reassert && P.reassert) ev.push_back({e.t + std::clamp(e.dt * 0.4, 0.005, 0.02), e.group, false, e.dt});
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
        const int master = group();
        const size_t kSeg = 100;
        for (size_t i = 0; i < ev.size(); i += kSeg) {
            const size_t e = std::min(ev.size(), i + kSeg);
            const int seg = group();
            const double tau = ev[i].t;
            for (size_t k = i; k < e; ++k) spawn(gs(seg), ev[k].group, std::max(0.0, ev[k].t - tau));
            spawn(gs(master), seg, tau);
        }
        spawn(gs(master), master, std::max(0.05, total));            // el bucle se relanza a si mismo
        spawnAt(P.startX, P.originY - 60.0, "", master, 0);           // arranque: lanza el bucle al empezar el nivel
    }

    std::vector<std::string> take() {
        trig.flush(); obj.flush();
        std::vector<std::string> out = std::move(trig.done);          // triggers primero
        for (auto& s : obj.done) out.push_back(std::move(s));
        return out;
    }

private:
    double parkX = 0, parkY = 0; int parkN = 0;
    std::string& head(int id, double x, double y) {
        std::string& o = trig.begin();
        o += "1,"; o += std::to_string(id);
        o += ",2,"; gdx::detail::appendNum(o, x, 2);
        o += ",3,"; gdx::detail::appendNum(o, y, 2);
        o += ",36,1";
        return o;
    }
    void tail(std::string& o, const std::string& own) {
        if (!own.empty()) { o += ",62,1,87,1,57,"; o += own; }
        trig.end(); ++nTrig;
    }
};

} // namespace vx
