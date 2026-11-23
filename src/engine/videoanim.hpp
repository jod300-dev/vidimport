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
//   REMAP (ultra ligero)    Un UNICO grupo plantilla T con 4 Toggle (mostrar 1 + ocultar 3) cuyos objetivos son 4 grupos "pin" sin objetos. M lanza
//                           Spawn(T, t_e, REMAP) por paso (clave 442: "pin.G_e.pin.G_a..."): GD sustituye los pines por los grupos de ese paso.
//                           Casi 1 grupo y 1 trigger por frame (los otros modos: 2 grupos y ~6 triggers) -> mas frames y mas segundos
//                           con los 9999 grupos de GD. Instantes ABSOLUTOS como Toggle (una sola ejecucion por paso). El anti-lag funciona:
//                           cada parte lleva ademas un grupo propio C_k y UN Move (entrada/salida) mueve la parte entera.
//
//  En los modos Toggle/Spawn MOSTRAR/OCULTAR es un TOGGLE absoluto (clave 56: 1 = activar, 0 = desactivar): los frames ocultos quedan
//  DESACTIVADOS (GD ni los dibuja ni los procesa) en vez de seguir dibujandose con opacidad 0 -> sin lag.
//
//  ANTI-LAG (opcional, "partes"): los frames se agrupan en PARTES de ~N objetos (p. ej. 25000). Solo UNA parte (o dos, un instante) esta
//  cerca de la camara: las demas ESPERAN JUNTAS Y LEJOS en X (sin Toggle de parte: solo la distancia las deja fuera de juego), asi GD no
//  recorre 450k objetos cada fotograma. Una parte entra con un Move instantaneo (-D, 0) un poco ANTES de su primer frame (invisible: sus
//  frames siguen ocultos por Toggle) y, cuando termina y su ultimo frame ya esta oculto, otro Move (+D, 0) la devuelve lejos. Hay UN Move
//  por grupo de frames y por sentido, dentro del paso F_e (ningun grupo nuevo). Los Move son RELATIVOS: un paso con Move se ejecuta UNA
//  sola vez por vuelta (en modo Toggle no lleva la segunda ejecucion de reafirmacion).
//
//  Robustez: z-order CRECIENTE con el tiempo (el frame nuevo tapa por completo a los anteriores: un "ocultar" perdido no se ve) y cada
//  paso oculta los 2 anteriores y 1 frame lejano (repartido por todo el bucle: cada frame se vuelve a ocultar una vez por vuelta).
// ============================================================================
#pragma once
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "gdexport.hpp"
#include "videoplan.hpp"
#include "videochan.hpp"

namespace gdx {

enum class PlayMode : int { Toggle = 0, Spawn = 1, Remap = 2 };

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

// Anti-lag: partes que esperan lejos y entran/salen con un Move instantaneo.
struct LagParams {
    bool enable = false;
    int chunkObjects = 25000;         // objetos por parte (una parte = frames consecutivos hasta sumar al menos esto; ~1 frame = flujo continuo)
    double leadSeconds = 0.5;         // el Move de entrada va tanto tiempo ANTES del primer frame de la parte
    double farDistance = 30000.0;     // unidades X a las que esperan (30000 = 1000 bloques; se agranda sola si el video es muy ancho)
    // Cada parte a una distancia DISTINTA (parte k a farDistance + k * farStep) en vez de todas juntas en el mismo sitio. Asi no hay un monton
    // de objetos apilados en una misma zona lejana. El paso se reduce solo para que ninguna parte pase de farMax.
    bool spread = false;
    double farStep = 30000.0;
    double farMax = 300000.0;
    int objectIdMove = 901;
};

// Resultado de la planificacion de partes (para el mensaje final y los tests).
struct LagInfo {
    bool requested = false, active = false;
    int chunks = 0;
    long moveTriggers = 0;
    double farDist = 0;
    std::string note;                 // por que no se aplico (si se pidio y no se pudo)
};

struct VideoExportParams {
    GdExportOptions eo;
    int baseGroup = 1;                // primer grupo libre
    double trigX = -30;               // x de los 2 triggers de arranque (en x <= 0 se ejecutan al empezar el nivel)
    PlayMode mode = PlayMode::Toggle;
    int objectIdToggle = 1049;
    int objectIdSpawn = 1268;
    int gridCols = 80;                // los triggers van en una rejilla (paso 30 u): ninguno comparte celda con otro
    // Los triggers van PEGADOS (paso 30 u en X e Y: uno al lado y uno encima del otro). La rejilla base de gridCols columnas se ENSANCHA en
    // spreadXBlocks columnas mas; las filas crecen las que hagan falta (sin tope), asi que arriba tambien cabe todo.
    int spreadXBlocks = 50;
    // Correccion de velocidad: todos los tiempos (retardos de los Spawn, duracion de la vuelta) se multiplican por esto. 1 = exacto.
    // Si en tu GD la animacion dura MENOS que el video, sube el valor (p. ej. 1.05); si dura mas, bajalo.
    double timeScale = 1.0;
    double blockSeconds = 1.0;        // modo Spawn: longitud de cada cadena entre resincronizaciones
    // Colores en CANALES (sin HSV): paleta global ya aplicada a los Result con quantizeVideo (r.colored = true). Vacia = HSV.
    std::vector<std::array<uint8_t, 3>> palette;
    int firstChannel = kVideoFirstChannel;
    LagParams lag;                    // partes anti-lag (lag.enable)
};

struct VEvent { double t; int group; };

struct VideoSchedule {
    std::vector<VEvent> ev;           // pasos del bucle (cambios de grupo), ordenados por t
    double total = 0;                 // duracion de la vuelta (s)
    std::vector<int> used;            // grupos que se muestran en algun paso (orden creciente)
    int groupsNeeded = 0;             // G + F + M + I0   (Remap: G + plantilla + 4 pines + M + I0; las partes anti-lag suman 3 por parte)
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
// Spawn con REMAP (clave 442): GD sustituye en el grupo lanzado los IDs `a` por `b` ("a.b.a.b"). Solo los pares con b > 0.
inline void spawnRemap(std::string& s, const VideoExportParams& p, double x, double y, int target, double delay, const std::vector<std::pair<int, int>>& rm, int own) {
    head(s, p.objectIdSpawn, x, y);
    s += ",51,"; s += std::to_string(target);
    if (delay > 0) { s += ",63,"; detail::appendNum(s, delay, 4); }
    if (!rm.empty()) {
        s += ",442,";
        for (size_t i = 0; i < rm.size(); ++i) { if (i) s += '.'; s += std::to_string(rm[i].first); s += '.'; s += std::to_string(rm[i].second); }
    }
    if (own > 0) { s += ",62,1,87,1,57,"; s += std::to_string(own); }
    s += ';';
}
// Move instantaneo (duracion 0) del grupo `target`: (dx, dy) en unidades de GD (30 = 1 bloque).
inline void move(std::string& s, const VideoExportParams& p, double x, double y, int target, double dx, double dy, int own) {
    head(s, p.lag.objectIdMove, x, y);
    s += ",28,"; detail::appendNum(s, dx, 2); s += ",29,"; detail::appendNum(s, dy, 2);
    s += ",10,0,51,"; s += std::to_string(target);
    s += ",62,1,87,1,57,"; s += std::to_string(own); s += ';';
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
struct ChunkPlan {
    bool active = false;
    std::string note;
    std::vector<std::vector<int>> groups;     // por parte: indices de grupo de resultado (los de S.used)
    std::vector<int> inStep, outStep;         // evento (paso F_e) donde va el Move de entrada / salida de cada parte
    std::vector<char> startsNear;             // la parte empieza en el origen: su intervalo "cerca" cruza el inicio de la vuelta (entrada al final de la anterior o salida en la siguiente)
    std::vector<double> farOf;                // distancia de espera de CADA parte (todas iguales salvo con spread)
    double farDist = 0;                       // la mayor
};

// Agrupa los grupos de frames en partes y decide en que paso entra y sale cada una. `objs[g]` = objetos del grupo g.
inline ChunkPlan planChunks(const VideoSchedule& S, const std::vector<size_t>& objs, int fps, const LagParams& lp, double imageWidthUnits) {
    ChunkPlan cp;
    const int E = int(S.ev.size());
    if (!lp.enable) { cp.note = "desactivado"; return cp; }
    if (S.still || E < 12) { cp.note = "el video tiene muy pocos pasos (<12)"; return cp; }
    const double total = S.total;
    // primer / ultimo paso en que se usa cada grupo
    std::map<int, int> fu, lu;
    for (int e = 0; e < E; ++e) { const int g = S.ev[size_t(e)].group; if (!fu.count(g)) fu[g] = e; lu[g] = e; }
    std::vector<int> order(S.used.begin(), S.used.end());
    std::sort(order.begin(), order.end(), [&](int a, int b) { return fu[a] < fu[b]; });
    // partes: grupos consecutivos (por primer uso) hasta sumar >= chunkObjects
    const size_t target = size_t(std::max(1, lp.chunkObjects));
    std::vector<std::vector<int>> parts; std::vector<size_t> sums;
    for (int g : order) {
        if (parts.empty() || sums.back() >= target) { parts.emplace_back(); sums.push_back(0); }
        parts.back().push_back(g); sums.back() += objs[size_t(g)];
    }
    if (parts.size() >= 2 && sums.back() < target / 4) {                 // cola minuscula: se une a la anterior
        for (int g : parts.back()) parts[parts.size() - 2].push_back(g);
        sums[sums.size() - 2] += sums.back(); parts.pop_back(); sums.pop_back();
    }
    if (parts.size() < 2) { cp.note = "todo el video cabe en una sola parte (baja \"objetos por parte\")"; return cp; }

    auto eventAtOrBefore = [&](double t) { int e = 0; for (int i = 0; i < E; ++i) if (S.ev[size_t(i)].t <= t + 1e-9) e = i; return e; };
    const double step = total / std::max(1, E);                           // margen minimo (un paso medio)
    for (auto& grp : parts) {
        int a = E, b = 0;
        for (int g : grp) { a = std::min(a, fu[g]); b = std::max(b, lu[g]); }
        // entrada: ultimo paso cuyo instante es <= t_a - lead (y siempre ANTES de a)
        double tin = S.ev[size_t(a)].t - lp.leadSeconds;
        int inE; bool wrapIn = false;
        if (tin < 0) { wrapIn = true; inE = eventAtOrBefore(total + tin); if (inE <= b + 2) inE = std::min(E - 1, b + 3); }   // al final de la vuelta anterior
        else { inE = eventAtOrBefore(tin); if (inE >= a) inE = std::max(0, a - 1); }
        // salida: 2 pasos despues del ultimo uso (el ultimo frame ya esta oculto)
        int outE = b + 2; bool wrapOut = false;
        if (outE >= E) { outE -= E; wrapOut = true; }
        // tiempo que la parte pasa cerca: debe caber en la vuelta con margen (si no, entrada y salida se pisarian)
        const double tI = wrapIn ? S.ev[size_t(inE)].t - total : S.ev[size_t(inE)].t;
        const double tO = wrapOut ? S.ev[size_t(outE)].t + total : S.ev[size_t(outE)].t;
        if (tO - tI > total - 2.0 * step) { cp.note = "las partes no caben en la vuelta (video demasiado corto para este tamano de parte)"; cp.groups.clear(); return cp; }
        cp.groups.push_back(grp); cp.inStep.push_back(inE); cp.outStep.push_back(outE); cp.startsNear.push_back((wrapIn || wrapOut) ? 1 : 0);   // el intervalo "cerca" cruza el inicio de la vuelta: en regimen estable empieza cada vuelta cerca
    }
    const double base = std::max(lp.farDistance, 2.0 * imageWidthUnits + 1500.0);
    const size_t K = cp.groups.size();
    double stepU = 0.0;
    if (lp.spread && K > 1) {
        stepU = std::max(0.0, lp.farStep);
        if (lp.farMax > base) stepU = std::min(stepU, (lp.farMax - base) / double(K - 1));   // la ultima parte no pasa de farMax
        else stepU = 0.0;
    }
    for (size_t k = 0; k < K; ++k) cp.farOf.push_back(base + double(k) * stepU);
    cp.farDist = cp.farOf.back() > cp.farOf.front() ? cp.farOf.back() : cp.farOf.front();
    cp.active = true;
    return cp;
}
} // namespace vdetail

// Pasos del bucle a partir del plan (solo depende del plan, no de los objetos). `gsrc` remapea grupos vacios.
inline VideoSchedule scheduleVideo(const VideoPlan& plan, PlayMode mode, const std::vector<int>* gsrc = nullptr, double blockSeconds = 1.0, double timeScale = 1.0) {
    VideoSchedule s;
    const int N = plan.frames;
    const double ts = timeScale > 0.0 ? timeScale : 1.0;
    s.total = plan.seconds() * ts;                               // la duracion REAL del tramo (no N / fps): el bucle dura lo mismo que el MP4
    int prev = -1;
    for (int i = 0; i < N; ++i) {
        int g = plan.group[size_t(i)];
        if (gsrc) g = (*gsrc)[size_t(g)];
        if (g != prev) { s.ev.push_back({double(i) / plan.fps * ts, g}); prev = g; }
    }
    if (s.ev.size() >= 2 && s.ev.front().group == s.ev.back().group) s.ev.pop_back();   // el bucle cierra: el ultimo paso ya es el primero
    std::set<int> u; for (auto& e : s.ev) u.insert(e.group);
    s.used.assign(u.begin(), u.end());
    const size_t E = s.ev.size();
    if (E <= 1) { s.still = true; s.groupsNeeded = 0; s.triggers = 0; return s; }
    if (mode == PlayMode::Remap) {                                                        // plantilla (4 Toggle) + 1 Spawn por paso + I0 en lotes de 3 + Spawn(M) + 2 de arranque
        s.triggers = 4 + long(E) + (long(s.used.size()) + 1) / 3 + 1 + 2;
        s.groupsNeeded = int(s.used.size()) + 4 + 1 + 2;                                  // G + 4 pines + plantilla + M + I0
        return s;
    }
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

// Triggers que suma el anti-lag en modo Remap: por parte, Spawn+Move de entrada y Spawn+Move de salida.
constexpr long kRemapLagTriggersPerChunk = 4;

// Rejilla de los triggers: `baseCols + extraX` columnas, filas las que hagan falta para `n` celdas, SIEMPRE con paso 30 u (1 bloque) en X e Y:
// los triggers quedan pegados, uno al lado y uno encima del otro, y ninguna celda se repite.
struct TriggerGrid { int cols = 8; int rows = 1; double pitchX = 30.0, pitchY = 30.0; };
inline TriggerGrid planTriggerGrid(long n, int baseCols, int extraX) {
    TriggerGrid g;
    g.cols = std::max(8, baseCols) + std::max(0, extraX);
    g.rows = int(std::max(1L, (std::max(1L, n) + g.cols - 1) / g.cols));
    return g;
}

// results = un Result por grupo del plan. Devuelve "" si algo no cuadra (errmsg explica).
inline std::string buildVideoString(const std::vector<Result>& results, const VideoPlan& plan, const VideoExportParams& p, std::string* errmsg = nullptr, LagInfo* lagInfo = nullptr) {
    auto fail = [&](const char* m) { if (errmsg) *errmsg = m; return std::string(); };
    if (lagInfo) *lagInfo = LagInfo{};
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
    const VideoSchedule S = scheduleVideo(plan, p.mode, &gsrc, p.blockSeconds, p.timeScale);
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
    const bool remap = p.mode == PlayMode::Remap;
    std::vector<int> F(E, 0);
    int T = 0; int pin[4] = {0, 0, 0, 0};
    if (remap) { T = next++; for (int k = 0; k < 4; ++k) pin[k] = next++; }                 // plantilla + 4 pines (grupos sin objetos)
    else for (size_t e = 0; e < E; ++e) F[e] = next++;
    const int M = next++, I0 = next++;

    // ---- partes anti-lag
    vdetail::ChunkPlan cp;
    LagInfo li; li.requested = p.lag.enable;
    std::vector<char> farGroup(size_t(U), 0);
    std::vector<double> farOfGroup(size_t(U), 0.0);                          // distancia de espera de cada grupo (la de su parte)
    std::vector<std::vector<std::pair<int, double>>> movesAt(E);            // paso -> (grupo, dx)
    std::vector<char> stepHasMove(E, 0);
    std::vector<int> chunkOfGroup(size_t(U), -1);                           // Remap: parte (0..K-1) de cada grupo de frames
    std::vector<int> Cg, MVin, MVout;                                       // Remap: grupo de cada parte y grupos de sus Move de entrada / salida
    if (p.lag.enable) {
        std::vector<size_t> objs(size_t(U), 0);
        for (int g : S.used) objs[size_t(g)] = results[size_t(g)].shapes.size();
        const GdSize sz0 = gdImageSize(results[size_t(S.ev[0].group)], p.eo);
        cp = vdetail::planChunks(S, objs, plan.fps, p.lag, sz0.w);
        li.active = cp.active; li.note = cp.note; li.chunks = int(cp.groups.size()); li.farDist = cp.farDist;
        if (cp.active) {
            for (size_t k = 0; k < cp.groups.size(); ++k) {
                for (int g : cp.groups[k]) {
                    if (!cp.startsNear[k]) { farGroup[size_t(g)] = 1; farOfGroup[size_t(g)] = cp.farOf[k]; }
                    if (remap) { chunkOfGroup[size_t(g)] = int(k); continue; }             // Remap: un Move por PARTE (grupo propio C_k), no por grupo de frames
                    movesAt[size_t(cp.inStep[k])].push_back({g, -cp.farOf[k]});
                    movesAt[size_t(cp.outStep[k])].push_back({g, +cp.farOf[k]});
                    li.moveTriggers += 2;
                }
            }
            if (remap) {
                for (size_t k = 0; k < cp.groups.size(); ++k) { Cg.push_back(next++); MVin.push_back(next++); MVout.push_back(next++); }
                li.moveTriggers = kRemapLagTriggersPerChunk * long(cp.groups.size());
                if (next - 1 > 9999) return fail("No quedan grupos libres suficientes para las partes anti-lag (max. 9999)");
            }
            for (size_t e = 0; e < E; ++e) stepHasMove[e] = movesAt[e].empty() ? 0 : 1;
        }
    }
    if (lagInfo) *lagInfo = li;

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
        if (remap && chunkOfGroup[size_t(g)] >= 0) { eo.groupStr += '.'; eo.groupStr += std::to_string(Cg[size_t(chunkOfGroup[size_t(g)])]); }   // grupo de la parte (un Move la mueve entera)
        if (farGroup[size_t(g)]) eo.originX = p.eo.originX + farOfGroup[size_t(g)];      // espera lejos hasta que su Move la traiga
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
    // celdas de la rejilla = triggers de los pasos (sin los 2 de arranque ni los Color triggers, que van aparte); cota por arriba
    const long gridCells = std::max(1L, S.triggers - 2 + li.moveTriggers);
    const TriggerGrid grid = planTriggerGrid(gridCells, p.gridCols, p.spreadXBlocks);
    int cell = 0;
    auto at = [&](double& x, double& y) { x = left + double(cell % grid.cols) * grid.pitchX; y = bottom + double(cell / grid.cols) * grid.pitchY; ++cell; };
    double x, y;

    if (remap) {
        // M: un Spawn(T, t_e, remap) por paso: pin0 -> grupo que se muestra, pin1..3 -> los que se ocultan (los pines sin mapear no tocan nada)
        for (size_t e = 0; e < E; ++e) {
            std::vector<std::pair<int, int>> rm; rm.push_back({pin[0], G[size_t(S.ev[e].group)]});
            const std::vector<int> hs = vdetail::hideSet(S.ev, e);
            for (size_t h = 0; h < hs.size() && h < 3; ++h) rm.push_back({pin[1 + h], G[size_t(hs[h])]});
            at(x, y); vdetail::spawnRemap(out, p, x, y, T, S.ev[e].t, rm, M);
        }
        // anti-lag: por parte, Spawn(grupo del Move) en el paso de entrada / salida + el Move en su grupo (1 Move mueve la parte entera)
        for (size_t k = 0; k < Cg.size(); ++k) {
            at(x, y); vdetail::spawn(out, p, x, y, MVin[k], S.ev[size_t(cp.inStep[k])].t, M);
            at(x, y); vdetail::spawn(out, p, x, y, MVout[k], S.ev[size_t(cp.outStep[k])].t, M);
            at(x, y); vdetail::move(out, p, x, y, Cg[k], -cp.farOf[k], 0.0, MVin[k]);
            at(x, y); vdetail::move(out, p, x, y, Cg[k], +cp.farOf[k], 0.0, MVout[k]);
        }
        at(x, y); vdetail::spawn(out, p, x, y, M, S.total, M);                                                     // la vuelta se relanza sola
        // plantilla T: mostrar pin0 y ocultar pin1..3
        at(x, y); vdetail::toggle(out, p, x, y, pin[0], 1, T);
        for (int k = 1; k < 4; ++k) { at(x, y); vdetail::toggle(out, p, x, y, pin[k], 0, T); }
        // inicio I0 (una sola vez): la plantilla en lotes de 3 oculta todo menos el primer frame (pin0 -> primer frame: se queda mostrado)
        std::vector<int> toHide; for (int g : S.used) if (g != S.ev[0].group) toHide.push_back(g);
        for (size_t i = 0; i < toHide.size(); i += 3) {
            std::vector<std::pair<int, int>> rm; rm.push_back({pin[0], G[size_t(S.ev[0].group)]});
            for (size_t j = 0; j < 3 && i + j < toHide.size(); ++j) rm.push_back({pin[1 + j], G[size_t(toHide[i + j])]});
            at(x, y); vdetail::spawnRemap(out, p, x, y, T, 0.0, rm, I0);
        }
    } else {
        // maestro M
        if (p.mode == PlayMode::Toggle) {
            const double rd = vdetail::reassert(plan.fps);
            for (size_t e = 0; e < E; ++e) { at(x, y); vdetail::spawn(out, p, x, y, F[e], S.ev[e].t, M); }
            for (size_t e = 0; e < E; ++e) {                                                                         // segunda ejecucion (idempotente)
                if (stepHasMove[e]) continue;                                                                         // ...salvo en los pasos con Move: es RELATIVO y se aplicaria dos veces
                at(x, y); vdetail::spawn(out, p, x, y, F[e], S.ev[e].t + rd, M);
            }
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
                for (auto& mv : movesAt[e]) { at(x, y); vdetail::move(out, p, x, y, G[size_t(mv.first)], mv.second, 0.0, F[e]); }     // anti-lag: parte que entra / sale
                if (p.mode == PlayMode::Spawn && e + 1 < E) {
                    const long b0 = long(std::floor(S.ev[e].t / bs + 1e-9)), b1 = long(std::floor(S.ev[e + 1].t / bs + 1e-9));
                    if (b0 == b1) { at(x, y); vdetail::spawn(out, p, x, y, F[e + 1], S.ev[e + 1].t - S.ev[e].t, F[e]); }  // encadenar con el paso siguiente
                }
            }
        }
        // inicio I0 (una sola vez): oculta todo menos el primer frame
        for (int g : S.used) if (g != S.ev[0].group) { at(x, y); vdetail::toggle(out, p, x, y, G[size_t(g)], 0, I0); }
    }
    // arranque (triggers de posicion), apilados a la izquierda
    vdetail::spawn(out, p, p.trigX, bottom, M, 0, 0);
    vdetail::spawn(out, p, p.trigX, bottom + 30.0, I0, 0, 0);
    if (!p.palette.empty()) appendColorTriggers(out, p.palette, p.firstChannel, p.trigX, bottom);   // colores de los canales (una sola vez, al inicio)

    detail::triggersFirst(out, objEnd);
    if (!out.empty() && out.back() == ';') out.pop_back();
    return out;
}

} // namespace gdx
