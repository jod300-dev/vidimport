// ============================================================================
//  gdx :: video -> cadena de objetos de GD  (solo Toggle triggers + Spawn triggers; NINGUN otro trigger)
//  Sin dependencias de Geode.
//
//  Grupos:   G_g  objetos del grupo de frames g          F_e  paso e (muestra G y oculta los demas)
//            M    maestro del bucle                       I0   inicio (oculta todo salvo el primer frame; solo una vez)
//
//  Los dos modos solo difieren en COMO se encadenan los pasos F_e (los objetos y los pasos son identicos):
//
//   TOGGLE (lista exacta)   M = Spawn(F_e, t_e) para TODOS los pasos (+ una segunda ejecucion unos ms despues) + Spawn(M, duracion).
//                           Cada paso tiene su instante ABSOLUTO medido desde el inicio de la vuelta: la unica imprecision es de un tick
//                           y nunca se acumula. Es el modo mas preciso; usa mas Spawn y los encola todos al empezar cada vuelta.
//
//   SPAWN (cadena)          Dentro de cada bloque de 1 s el paso e lanza al siguiente (Spawn(F_{e+1}, dt)); M solo lanza la CABEZA de
//                           cada bloque en su instante absoluto (resincroniza cada segundo) y se relanza. Mitad de Spawn, carga
//                           constante por frame (sin pico al empezar la vuelta). Si GD pierde un Spawn la cadena se repara sola
//                           en el siguiente segundo.
//
//  En los dos modos MOSTRAR/OCULTAR es un TOGGLE absoluto (clave 56: 1 = activar, 0 = desactivar): los frames ocultos quedan
//  DESACTIVADOS (GD ni los dibuja ni los procesa) en vez de seguir dibujandose con opacidad 0 -> sin lag.
//
//  Robustez: z-order CRECIENTE con el tiempo (el frame nuevo tapa por completo a los anteriores: un "ocultar" perdido no se ve) y cada
//  paso oculta los 2 anteriores y 1 frame lejano (repartido por todo el bucle: cada frame se vuelve a ocultar una vez por vuelta).
// ============================================================================
#pragma once
#include <algorithm>
#include <set>
#include <string>
#include <vector>
#include "gdexport.hpp"
#include "videoplan.hpp"
#include "videochan.hpp"

namespace gdx {

enum class PlayMode : int { Toggle = 0, Spawn = 1 };

namespace detail {
// Mueve la parte de triggers (out[objEnd..]) delante de los objetos (out[0..objEnd)). Ambas partes terminan en ';'.
// Si GD corta la cadena por el limite de objetos del editor, se pierden OBJETOS del final, nunca los triggers.
inline void triggersFirst(std::string& out, size_t objEnd) {
    if (objEnd == 0 || objEnd >= out.size()) return;
    if (out[objEnd - 1] != ';') return;
    std::string t = out.substr(objEnd);
    if (t.empty() || t.back() != ';') t += ';';
    out.erase(objEnd);
    out.insert(0, t);
}
} // namespace detail

struct VideoExportParams {
    GdExportOptions eo;
    int baseGroup = 1;                // primer grupo libre
    double trigX = -30;               // x de los 2 triggers de arranque (en x <= 0 se ejecutan al empezar el nivel)
    PlayMode mode = PlayMode::Toggle;
    int objectIdToggle = 1049;
    int objectIdSpawn = 1268;
    int gridCols = 80;                // los triggers van en una rejilla compacta (paso 30 u): ninguno comparte celda con otro
    double blockSeconds = 1.0;        // modo Spawn: longitud de cada cadena entre resincronizaciones
    // Colores en CANALES (sin HSV): paleta global ya aplicada a los Result con quantizeVideo (r.colored = true). Vacia = HSV.
    std::vector<std::array<uint8_t, 3>> palette;
    int firstChannel = kVideoFirstChannel;
};

struct VEvent { double t; int group; };

struct VideoSchedule {
    std::vector<VEvent> ev;           // pasos del bucle (cambios de grupo), ordenados por t
    double total = 0;                 // duracion de la vuelta (s)
    std::vector<int> used;            // grupos que se muestran en algun paso (orden creciente)
    int groupsNeeded = 0;             // G + F + M + I0
    long triggers = 0;                // cantidad EXACTA de triggers
    bool still = false;               // un solo grupo: imagen fija, sin triggers
};

namespace vdetail {

inline void head(std::string& s, int id, double x, double y) {
    s += "1,"; s += std::to_string(id); s += ",2,"; detail::appendNum(s, x, 2); s += ",3,"; detail::appendNum(s, y, 2); s += ",36,1";
}
// Toggle absoluto: on=1 activa el grupo, on=0 lo desactiva. own = grupo "spawn triggered" donde vive el trigger.
inline void toggle(std::string& s, const VideoExportParams& p, double x, double y, int target, int on, int own) {
    head(s, p.objectIdToggle, x, y);
    s += ",51,"; s += std::to_string(target); s += ",56,"; s += on ? '1' : '0';
    s += ",62,1,87,1,57,"; s += std::to_string(own); s += ';';
}
// own <= 0: trigger de POSICION (se activa al llegar a x); si no, "spawn triggered" dentro de ese grupo.
inline void spawn(std::string& s, const VideoExportParams& p, double x, double y, int target, double delay, int own) {
    head(s, p.objectIdSpawn, x, y);
    s += ",51,"; s += std::to_string(target);
    if (delay > 0) { s += ",63,"; detail::appendNum(s, delay, 4); }
    if (own > 0) { s += ",62,1,87,1,57,"; s += std::to_string(own); }
    s += ';';
}
inline double reassert(double fps) { return std::max(0.005, std::min(0.02, 0.4 / std::max(1.0, fps))); }

// Hasta 3 grupos a ocultar en el paso e (distintos entre si y del que se muestra).
inline std::vector<int> hideSet(const std::vector<VEvent>& ev, size_t e) {
    const size_t E = ev.size();
    std::vector<int> v;
    std::set<int> seen; seen.insert(ev[e].group);
    auto add = [&](size_t k) { const int g = ev[k % E].group; if (seen.insert(g).second) v.push_back(g); };
    if (E >= 2) add((e + E - 1) % E);
    if (E >= 3) add((e + E - 2) % E);
    if (E >= 6) add(e + E / 2);                                 // lejano: repartido por todo el bucle
    return v;
}
} // namespace vdetail

// Pasos del bucle a partir del plan (solo depende del plan, no de los objetos). `gsrc` remapea grupos vacios.
inline VideoSchedule scheduleVideo(const VideoPlan& plan, PlayMode mode, const std::vector<int>* gsrc = nullptr, double blockSeconds = 1.0) {
    VideoSchedule s;
    const int N = plan.frames;
    s.total = plan.fps > 0 ? double(N) / plan.fps : 0.0;
    int prev = -1;
    for (int i = 0; i < N; ++i) {
        int g = plan.group[size_t(i)];
        if (gsrc) g = (*gsrc)[size_t(g)];
        if (g != prev) { s.ev.push_back({double(i) / plan.fps, g}); prev = g; }
    }
    if (s.ev.size() >= 2 && s.ev.front().group == s.ev.back().group) s.ev.pop_back();   // el bucle cierra: el ultimo paso ya es el primero
    std::set<int> u; for (auto& e : s.ev) u.insert(e.group);
    s.used.assign(u.begin(), u.end());
    const size_t E = s.ev.size();
    if (E <= 1) { s.still = true; s.groupsNeeded = 0; s.triggers = 0; return s; }
    long trig = 0;
    for (size_t e = 0; e < E; ++e) trig += 1 + long(vdetail::hideSet(s.ev, e).size());   // toggles de cada paso
    trig += long(s.used.size()) - 1;                                                      // I0
    trig += 2;                                                                            // arranque: Spawn(M) + Spawn(I0)
    if (mode == PlayMode::Toggle) {
        trig += 2 * long(E) + 1;                                                          // Spawn(F_e) x2 + Spawn(M, total)
    } else {
        long blocks = 0, chain = 0; long lastBlock = -1;
        for (size_t e = 0; e < E; ++e) {
            const long b = long(std::floor(s.ev[e].t / std::max(0.05, blockSeconds) + 1e-9));
            if (b != lastBlock) { ++blocks; lastBlock = b; } else ++chain;
        }
        trig += blocks + chain + 1;                                                       // cabezas + encadenados + Spawn(M, total)
    }
    s.triggers = trig;
    s.groupsNeeded = int(s.used.size()) + int(E) + 2;
    return s;
}

inline long videoTriggerCount(const VideoPlan& plan, PlayMode mode) { return scheduleVideo(plan, mode).triggers; }

// results = un Result por grupo del plan. Devuelve "" si algo no cuadra (errmsg explica).
inline std::string buildVideoString(const std::vector<Result>& results, const VideoPlan& plan, const VideoExportParams& p, std::string* errmsg = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    const int U = plan.groups();
    if (U < 1 || int(results.size()) != U || plan.frames < 1) return fail("No hay frames convertidos");

    // grupos vacios (error al convertir): se muestra el ultimo grupo con objetos (o el primero) en vez de dejar un hueco
    std::vector<int> gsrc(size_t(U), -1);
    {
        int last = -1;
        for (int g = 0; g < U; ++g) { if (!results[size_t(g)].shapes.empty()) last = g; gsrc[size_t(g)] = last; }
        int first = -1; for (int g = 0; g < U; ++g) if (!results[size_t(g)].shapes.empty()) { first = g; break; }
        if (first < 0) return fail("Ningun frame genero objetos");
        for (int g = 0; g < U && gsrc[size_t(g)] < 0; ++g) gsrc[size_t(g)] = first;
    }
    const VideoSchedule S = scheduleVideo(plan, p.mode, &gsrc, p.blockSeconds);
    const size_t E = S.ev.size();
    if (!p.palette.empty() && (p.firstChannel < 2 || p.firstChannel + int(p.palette.size()) - 1 > 999)) return fail("No caben los canales de color (2..999)");
    if (S.groupsNeeded > 0 && (p.baseGroup < 1 || p.baseGroup + S.groupsNeeded - 1 > 9999)) return fail("No quedan grupos libres suficientes (max. 9999)");

    size_t total = 0;
    for (int g : S.used) total += results[size_t(g)].shapes.size();
    if (S.still) total = results[size_t(S.ev.empty() ? 0 : S.ev[0].group)].shapes.size();
    std::string out;
    out.reserve(total * 100 + size_t(S.triggers) * 90);

    // ---- imagen fija (un solo grupo): sin grupos ni triggers
    if (S.still) {
        const int g = S.ev.empty() ? gsrc[0] : S.ev[0].group;
        GdExportOptions eo = p.eo; eo.groupStr.clear(); eo.monoFastPath = false;
        const Result& rs = results[size_t(g)];
        if (!p.palette.empty()) {
            const ChannelMap cm = buildChannelMap(rs);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t k = 0; k < cm.colors.size(); ++k)
                for (size_t j = 0; j < p.palette.size(); ++j) if (p.palette[j] == cm.colors[k]) { eo.channels[k] = p.firstChannel + int(j); break; }
        }
        out = toGdString(rs, eo);
        if (!p.palette.empty()) { out += ';'; appendColorTriggers(out, p.palette, p.firstChannel, p.trigX, p.eo.originY - 30.0); if (out.back() == ';') out.pop_back(); }
        return out;
    }

    // ---- ids de grupo
    int next = p.baseGroup;
    std::vector<int> G(size_t(U), 0);
    for (int g : S.used) G[size_t(g)] = next++;
    std::vector<int> F(E, 0);
    for (size_t e = 0; e < E; ++e) F[e] = next++;
    const int M = next++, I0 = next++;

    // ---- z-order creciente: banda propia por grupo (el frame nuevo tapa del todo a los anteriores)
    std::vector<int> zb(size_t(U), p.eo.zBase);
    {
        long acc = p.eo.zBase; std::vector<int> span(size_t(U), 1); long sum = 0;
        for (int g : S.used) { int sp = 1; for (auto& sh : results[size_t(g)].shapes) sp = std::max<int>(sp, sh.z + 1); span[size_t(g)] = sp; sum += sp; }
        if (sum <= 30000) for (int g : S.used) { zb[size_t(g)] = int(acc); acc += span[size_t(g)]; }
    }
    for (int g : S.used) {
        const Result& r = results[size_t(g)];
        GdExportOptions eo = p.eo;
        eo.groupStr = std::to_string(G[size_t(g)]);
        eo.zBase = zb[size_t(g)];
        eo.monoFastPath = false;
        if (!p.palette.empty()) {                                  // canal de cada color de ESTE grupo = canal global de la paleta
            const ChannelMap cm = buildChannelMap(r);
            eo.channels.assign(cm.colors.size(), 0);
            for (size_t k = 0; k < cm.colors.size(); ++k) {
                int ch = p.firstChannel;
                for (size_t j = 0; j < p.palette.size(); ++j) if (p.palette[j] == cm.colors[k]) { ch = p.firstChannel + int(j); break; }
                eo.channels[k] = ch;
            }
        }
        std::string s = toGdString(r, eo);
        if (!s.empty()) { out += s; out += ';'; }
    }

    // ---- triggers (se mueven al principio de la cadena al final: si GD corta por el limite de objetos, se pierden objetos, no triggers)
    const size_t objEnd = out.size();
    const Result& r0 = results[size_t(S.ev[0].group)];
    const GdSize sz = gdImageSize(r0, p.eo);
    const double left = std::max(p.eo.originX - sz.w * 0.5, p.trigX + 60.0), bottom = p.eo.originY + sz.h * 0.5 + 90.0;
    const int cols = std::max(8, p.gridCols);
    int cell = 0;
    auto at = [&](double& x, double& y) { x = left + double(cell % cols) * 30.0; y = bottom + double(cell / cols) * 30.0; ++cell; };
    double x, y;

    // maestro M
    if (p.mode == PlayMode::Toggle) {
        const double rd = vdetail::reassert(plan.fps);
        for (size_t e = 0; e < E; ++e) { at(x, y); vdetail::spawn(out, p, x, y, F[e], S.ev[e].t, M); }
        for (size_t e = 0; e < E; ++e) { at(x, y); vdetail::spawn(out, p, x, y, F[e], S.ev[e].t + rd, M); }    // segunda ejecucion (idempotente)
    } else {
        const double bs = std::max(0.05, p.blockSeconds);
        long lastBlock = -1;
        for (size_t e = 0; e < E; ++e) {
            const long b = long(std::floor(S.ev[e].t / bs + 1e-9));
            if (b != lastBlock) { lastBlock = b; at(x, y); vdetail::spawn(out, p, x, y, F[e], S.ev[e].t, M); }     // cabeza del bloque: instante absoluto
        }
    }
    at(x, y); vdetail::spawn(out, p, x, y, M, S.total, M);                                                         // la vuelta se relanza sola

    // pasos F_e
    {
        const double bs = std::max(0.05, p.blockSeconds);
        for (size_t e = 0; e < E; ++e) {
            at(x, y); vdetail::toggle(out, p, x, y, G[size_t(S.ev[e].group)], 1, F[e]);                            // primero MOSTRAR el nuevo
            for (int h : vdetail::hideSet(S.ev, e)) { at(x, y); vdetail::toggle(out, p, x, y, G[size_t(h)], 0, F[e]); }
            if (p.mode == PlayMode::Spawn && e + 1 < E) {
                const long b0 = long(std::floor(S.ev[e].t / bs + 1e-9)), b1 = long(std::floor(S.ev[e + 1].t / bs + 1e-9));
                if (b0 == b1) { at(x, y); vdetail::spawn(out, p, x, y, F[e + 1], S.ev[e + 1].t - S.ev[e].t, F[e]); }  // encadenar con el paso siguiente
            }
        }
    }
    // inicio I0 (una sola vez): oculta todo menos el primer frame
    for (int g : S.used) if (g != S.ev[0].group) { at(x, y); vdetail::toggle(out, p, x, y, G[size_t(g)], 0, I0); }
    // arranque (triggers de posicion), apilados a la izquierda
    vdetail::spawn(out, p, p.trigX, bottom, M, 0, 0);
    vdetail::spawn(out, p, p.trigX, bottom + 30.0, I0, 0, 0);
    if (!p.palette.empty()) appendColorTriggers(out, p.palette, p.firstChannel, p.trigX, bottom);   // colores de los canales (una sola vez, al inicio)

    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
