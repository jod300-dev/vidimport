// ============================================================================
//  Diagnostico: "Guardar log al jugar" (ajuste play-log).
//
//  Si al darle a Jugar el nivel no entra y GD te devuelve a la pagina del nivel, este log dice QUE paso:
//    * al pulsar Jugar (pagina del nivel) y Guardar y jugar (editor): tamano de la cadena del nivel, objetos, X maxima, triggers...
//    * al empezar PlayLayer::init: nombre y tamano del nivel; al terminar: si devolvio true/false y cuanto tardo
//    * si el nivel termino de cargar (PlayLayer::setupHasCompleted)
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
    std::chrono::steady_clock::time_point g_initStart;
    double msSince(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
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
