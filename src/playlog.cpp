// ============================================================================
//  Diagnostico: "Guardar log al jugar" (ajuste play-log).
//
//  Si al darle a Jugar el nivel no entra y GD te devuelve a la pagina del nivel, este log dice QUE paso:
//    * al pulsar Jugar (pagina del nivel) y Guardar y jugar (editor): tamano de la cadena del nivel, objetos, X maxima, triggers...
//    * al empezar PlayLayer::init: nombre y tamano del nivel; al terminar: si devolvio true/false y cuanto tardo
//    * si el nivel termino de cargar (PlayLayer::setupHasCompleted)
//    * DESPUES de cargar: cada 5 s de juego, fps medio, peor fotograma y cuantos fotogramas pasaron de 33 / 50 ms (mide el LAG de verdad),
//      los picos de mas de 100 ms, y como termina la partida (salir, completar el nivel, o volver a la pagina del nivel)
//    * si GD vuelve a la pagina del nivel SIN haber terminado de cargar -> se guarda ademas el final del log de Geode
//  Archivo: <carpeta de datos del mod>/play-log.txt  (en Geode: pestana del mod -> "Abrir carpeta de datos").
//  Cada linea se escribe y se cierra al momento: si GD se cierra de golpe, lo anterior queda guardado.
// ============================================================================
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace geode::prelude;

namespace {
    bool enabled() { return Mod::get()->getSettingValue<bool>("play-log"); }
    std::filesystem::path logPath() { return Mod::get()->getSaveDir() / "play-log.txt"; }

    std::string stamp() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char b[40];
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
        return b;
    }

    void append(std::string const& text) {
        try {
            const auto path = logPath();
            std::error_code ec;
            if (std::filesystem::exists(path, ec) && std::filesystem::file_size(path, ec) > 3u * 1024u * 1024u)
                std::filesystem::remove(path, ec);                    // el log no crece sin limite
            std::ofstream f(path, std::ios::app | std::ios::binary);
            if (f) f << "[" << stamp() << "] " << text << "\n";
        } catch (...) {}
    }

    // Final del log MAS RECIENTE de Geode (<juego>/geode/logs): ahi salen los avisos/errores de Geode y de los mods durante la carga.
    std::string geodeLogTail(size_t maxBytes) {
        try {
            const auto dir = Mod::get()->getSaveDir().parent_path().parent_path() / "logs";   // geode/config|mods/<id> -> geode/logs
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) return "(no se encontro la carpeta de logs de Geode)";
            std::filesystem::path newest;
            std::filesystem::file_time_type best{};
            for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
                if (!e.is_regular_file(ec) || e.path().extension() != ".log") continue;
                const auto t = e.last_write_time(ec);
                if (newest.empty() || t > best) { best = t; newest = e.path(); }
            }
            if (newest.empty()) return "(no hay logs de Geode)";
            std::ifstream f(newest, std::ios::binary);
            if (!f) return "(no se pudo abrir " + newest.string() + ")";
            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            const std::streamoff start = size > std::streamoff(maxBytes) ? size - std::streamoff(maxBytes) : 0;
            f.seekg(start);
            std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            return "----- final de " + newest.filename().string() + " (" + std::to_string(s.size()) + " bytes) -----\n" + s + "\n----- fin del log de Geode -----";
        } catch (...) { return "(error al leer el log de Geode)"; }
    }

    std::string levelInfo(GJGameLevel* level) {
        if (!level) return "nivel: (null)";
        std::ostringstream o;
        const std::string name = level->m_levelName;
        o << "nivel \"" << name << "\" | cadena del nivel: " << level->m_levelString.size() << " bytes ("
          << (level->m_levelString.size() / (1024.0 * 1024.0)) << " MB comprimida)";
        return o.str();
    }

    std::string editorStats() {
        auto lel = LevelEditorLayer::get();
        if (!lel || !lel->m_objects) return "(sin editor)";
        long n = 0, trig = 0; float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            ++n;
            const int id = obj->m_objectID;
            if (id == 1049 || id == 1268 || id == 899 || id == 901) ++trig;
            minX = std::min(minX, obj->getPositionX()); maxX = std::max(maxX, obj->getPositionX());
            minY = std::min(minY, obj->getPositionY()); maxY = std::max(maxY, obj->getPositionY());
        }
        std::ostringstream o;
        o << "editor: " << n << " objetos (" << trig << " triggers Toggle/Spawn/Color/Move) | X " << minX << " .. " << maxX << " | Y " << minY << " .. " << maxY;
        return o.str();
    }

    std::string lastPlacement() {
        auto s = Mod::get()->getSavedValue<std::string>("last-placement", "");
        return s.empty() ? "ultima colocacion del mod: (ninguna en esta instalacion)" : "ultima colocacion del mod: " + s;
    }

    bool g_pendingLoad = false;                                  // PlayLayer::init empezo y aun no hubo setupHasCompleted
    bool g_playing = false;                                      // el nivel cargo y se esta jugando
    std::chrono::steady_clock::time_point g_playStart;

    // Estadisticas de fotogramas (dt de PlayLayer::update): se vuelcan al log cada 5 s de juego y al salir.
    struct FrameStats {
        double sum = 0, worst = 0; long n = 0, over33 = 0, over50 = 0, over100 = 0;
        double wallStart = 0;
        long spikesLogged = 0;
        void reset() { sum = worst = 0; n = over33 = over50 = over100 = 0; }
    } g_fs;
    std::chrono::steady_clock::time_point g_initStart;
    double msSince(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }
    void flushStats(char const* why) {
        if (g_fs.n == 0) return;
        std::ostringstream o;
        const double avg = g_fs.sum / double(g_fs.n);
        o << "juego (" << why << ") t=" << (msSince(g_playStart) / 1000.0) << " s | " << g_fs.n << " fotogramas | fps medio " << (avg > 0 ? 1.0 / avg : 0.0)
          << " | peor fotograma " << (g_fs.worst * 1000.0) << " ms | >33 ms: " << g_fs.over33 << " | >50 ms: " << g_fs.over50 << " | >100 ms: " << g_fs.over100;
        append(o.str());
        g_fs.reset();
    }
    void endPlay(std::string const& how) {
        if (!g_playing) return;
        flushStats("final");
        std::ostringstream o;
        o << "FIN de la partida: " << how << " tras " << (msSince(g_playStart) / 1000.0) << " s de juego";
        append(o.str());
        g_playing = false;
    }
}

class $modify(PlayLogPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!enabled()) return PlayLayer::init(level, useReplay, dontCreateObjects);
        append("PlayLayer::init INICIO | " + levelInfo(level) + " | " + lastPlacement());
        g_pendingLoad = true;
        g_initStart = std::chrono::steady_clock::now();
        const bool ok = PlayLayer::init(level, useReplay, dontCreateObjects);
        std::ostringstream o;
        o << "PlayLayer::init FIN | devolvio " << (ok ? "true" : "FALSE") << " tras " << msSince(g_initStart) << " ms";
        append(o.str());
        if (!ok) {
            g_pendingLoad = false;
            append("!! PlayLayer::init devolvio FALSE: GD no pudo crear el nivel. Aqui esta el final del log de Geode:\n" + geodeLogTail(150 * 1024));
        }
        return ok;
    }

    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        if (!enabled()) return;
        g_pendingLoad = false;
        std::ostringstream o;
        o << "PlayLayer::setupHasCompleted | el nivel CARGO entero (" << msSince(g_initStart) << " ms desde init)";
        append(o.str());
        g_playing = true; g_fs.reset(); g_fs.spikesLogged = 0; g_playStart = std::chrono::steady_clock::now();
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if (!g_playing || !enabled() || dt <= 0.f) return;
        g_fs.sum += dt; ++g_fs.n; g_fs.worst = std::max<double>(g_fs.worst, dt);
        if (dt > 0.033f) ++g_fs.over33;
        if (dt > 0.050f) ++g_fs.over50;
        if (dt > 0.100f) {
            ++g_fs.over100;
            if (g_fs.spikesLogged++ < 60) {                       // picos grandes: aparte, con el instante (suelen coincidir con un Move de una parte)
                std::ostringstream o;
                o << "  pico de " << (dt * 1000.0) << " ms en t=" << (msSince(g_playStart) / 1000.0) << " s";
                append(o.str());
            }
        }
        if (g_fs.sum >= 5.0) flushStats("5 s");
    }

    void onQuit() {
        if (enabled()) endPlay("el jugador salio (PlayLayer::onQuit)");
        PlayLayer::onQuit();
    }

    void levelComplete() {
        if (enabled() && g_playing) { flushStats("nivel completado"); append("PlayLayer::levelComplete: nivel completado"); }
        PlayLayer::levelComplete();
    }
};

class $modify(PlayLogLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        const bool ok = LevelInfoLayer::init(level, challenge);
        if (enabled() && g_pendingLoad) {                        // volvemos a la pagina del nivel sin que el nivel terminara de cargar
            g_pendingLoad = false;
            std::ostringstream o;
            o << "!! Volvio a la pagina del nivel SIN terminar de cargar (a los " << msSince(g_initStart) << " ms de empezar PlayLayer::init). "
              << levelInfo(level) << "\nFinal del log de Geode:\n" << geodeLogTail(150 * 1024);
            append(o.str());
        }
        else if (enabled() && g_playing) {                       // volvimos a la pagina del nivel SIN pasar por onQuit
            const double secs = msSince(g_playStart) / 1000.0;
            endPlay("volvio a la pagina del nivel (sin onQuit: GD lo cerro por su cuenta?)");
            if (secs < 15.0) append("Final del log de Geode (la partida duro solo " + std::to_string(secs) + " s):\n" + geodeLogTail(150 * 1024));
        }
        return ok;
    }

    void onPlay(CCObject* sender) {
        if (enabled()) append("LevelInfoLayer::onPlay (boton Jugar de la pagina del nivel) | " + levelInfo(m_level));
        LevelInfoLayer::onPlay(sender);
    }
};

class $modify(PlayLogEditorPause, EditorPauseLayer) {
    void onSaveAndPlay(CCObject* sender) {
        if (enabled()) append("EditorPauseLayer::onSaveAndPlay (Guardar y jugar) | " + editorStats() + " | " + lastPlacement());
        EditorPauseLayer::onSaveAndPlay(sender);
    }
};
