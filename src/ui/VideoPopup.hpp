#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "engine.hpp"
#include "gdexport.hpp"
#include "videoanim.hpp"
#include "videoplan.hpp"
#include "videosource.hpp"

using namespace geode::prelude;

// Ventana del importador de video (430x290):
//   izquierda: vista previa del primer frame convertido + estado
//   derecha: metodo, objetos totales, lado largo, inicio y duracion; checks 12/30 fps, Toggle/Spawn/Remap y grises
class VideoPopup : public geode::Popup {
public:
    static VideoPopup* create(EditorUI* editor);
    ~VideoPopup();

protected:
    // Vista previa: decodifica UN frame del instante de inicio y lo convierte con el presupuesto medio por frame
    struct PreviewJob {
        gdx::Progress prog;
        int id = 0;
        std::string path;
        vid::Info info;
        double start = 0;
        int fps = 12, storeLong = 480;
        std::string ffmpegPath;
        bool allowFfmpeg = true;
        gdx::Method method = gdx::Method::BSP;
        gdx::Options opt;
        gdx::Result result;
        std::string error, backend;
        std::atomic<bool> done{false};
    };
    // Conversion completa (al pulsar Colocar): decodificar -> estabilizar -> planificar -> convertir
    struct BuildJob {
        gdx::Progress prog;                          // fase 2 (convertir)
        std::atomic<float> decodeProg{0.f};          // fase 1 (decodificar)
        std::atomic<int> phase{0};                   // 0 decodificando, 1 convirtiendo
        std::string path;
        vid::Info info;
        vid::SampleSpec spec;
        gdx::VideoPlanOptions po;
        gdx::Method method = gdx::Method::BSP;
        gdx::Options opt;
        int threads = 2;
        int fps = 12;
        bool chan = false;                           // colores en canales (sin HSV)
        bool lag = false;                            // partes anti-lag
        gdx::LagParams lagParams;
        int paletteSize = 128;
        std::vector<std::array<uint8_t, 3>> palette;
        std::atomic<bool> cancelDecode{false};
        gdx::VideoPlan plan;
        std::vector<gdx::Result> results;
        std::string error, backend;
        int srcW = 0, srcH = 0;
        std::atomic<bool> done{false};
    };

    bool init(EditorUI* editor);

    void onPick(CCObject*);
    void onPlace(CCObject*);
    void onInfo(CCObject*);
    void onSettings(CCObject*);
    void onPrev(CCObject*) { stepMethod(-1); }
    void onNext(CCObject*) { stepMethod(+1); }
    void onTotalSlider(CCObject* sender);
    void onSizeSlider(CCObject* sender);
    void onStartSlider(CCObject* sender);
    void onLenSlider(CCObject* sender);
    void onFps12(CCObject*) { setFps(12); }
    void onFps30(CCObject*) { setFps(30); }
    void onModeToggle(CCObject*) { setMode(gdx::PlayMode::Toggle); }
    void onModeSpawn(CCObject*) { setMode(gdx::PlayMode::Spawn); }
    void onModeRemap(CCObject*) { setMode(gdx::PlayMode::Remap); }
    void onGray(CCObject*);
    void onChan(CCObject*);
    void onLag(CCObject*);

    void setFps(int fps);
    void setMode(gdx::PlayMode m);
    void syncChecks(float = 0.f);                    // los toggles cambian DESPUES del callback: se fuerzan al estado real (casillas excluyentes)
    void stepMethod(int dir);
    void loadVideo(std::string const& path);
    void clampClip();                                // acota inicio / duracion al video y al presupuesto
    void syncSliders();
    void refreshLabels();
    void scheduleRecompute();
    void recompute(float = 0.f);
    void tick(float);
    void applyPreview(std::shared_ptr<PreviewJob> job);
    void buildPreview();
    void finishBuild(std::shared_ptr<BuildJob> job);
    void setStatus(std::string const& a, std::string const& b = "", std::string const& note = "");

    gdx::Method method() const;
    gdx::Options makeOptions(int budget) const;
    gdx::GdExportOptions makeExportOptions() const;
    gdx::VideoPlanOptions makePlanOptions() const;
    int storeLong(int frames) const;                 // lado largo con el que se guardan los frames (baja solo si hay muchos)
    int framesNow() const;
    int paletteSize() const;                         // colores de la paleta global (ajuste); 0 si el check de canales esta apagado
    int maxTotal() const;                            // tope efectivo: ajuste/deslizador y lo que cabe en el editor
    int editorRoom() const;
    double maxLengthFit() const;                     // duracion MAXIMA del deslizador (suelo absoluto de objetos por frame y limite de grupos)
    double softLengthFit() const;                    // duracion RECOMENDADA (minimo por frame de Ajustes)
    int perFrameEstimate() const;
    void startPlacement(std::string const& chain, size_t objectCount, std::string const& doneMsg);
    CCPoint placementOrigin() const;
    int maxUsedGroup() const;

    EditorUI* m_editor = nullptr;
    std::string m_path;
    vid::Info m_info;
    bool m_haveVideo = false;
    std::shared_ptr<PreviewJob> m_job;
    std::shared_ptr<BuildJob> m_build;
    int m_jobCounter = 0;
    gdx::Result m_result;
    bool m_hasResult = false;

    int m_method = 0;                      // indice en la lista de metodos de la ventana (0 = BSP)
    int m_groupBase = 1;                   // primer grupo libre del nivel (para el limite de 9999 grupos)
    int m_total = 1000000;                  // objetos totales
    int m_sizeU = 600;                     // lado largo, unidades GD
    double m_start = 0;                    // s
    double m_length = 5;                   // s
    int m_fps = 12;                        // 12 o 30
    gdx::PlayMode m_mode = gdx::PlayMode::Toggle;
    bool m_gray = false;
    bool m_chan = false;                   // colores en canales (sin HSV) en vez de HSV
    bool m_lag = false;                    // partes anti-lag (Move): solo una parte cerca de la camara

    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_val[4] = {nullptr, nullptr, nullptr, nullptr};
    Slider* m_totalSlider = nullptr;
    Slider* m_sizeSlider = nullptr;
    Slider* m_startSlider = nullptr;
    Slider* m_lenSlider = nullptr;
    CCMenuItemToggler* m_chk12 = nullptr;
    CCMenuItemToggler* m_chk30 = nullptr;
    CCMenuItemToggler* m_chkToggle = nullptr;
    CCMenuItemToggler* m_chkSpawn = nullptr;
    CCMenuItemToggler* m_chkRemap = nullptr;
    CCMenuItemToggler* m_chkGray = nullptr;
    CCMenuItemToggler* m_chkChan = nullptr;
    CCMenuItemToggler* m_chkLag = nullptr;
    CCLabelBMFont* m_statA = nullptr;
    CCLabelBMFont* m_statB = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
    std::string m_lastDesc;
    std::string m_loadNote;                // aviso del archivo (codec raro...)
};
