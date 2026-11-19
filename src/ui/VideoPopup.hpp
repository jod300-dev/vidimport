#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "director.hpp"
#include "source.hpp"

using namespace geode::prelude;

// Ventana del importador de VIDEO (440x300):
//   izquierda: vista previa del frame central pintado con el metodo elegido + estado
//   derecha: metodo (8), presupuesto de objetos, duracion, checks 12 fps / 30 fps, gris, canales
class VideoPopup : public geode::Popup {
public:
    static VideoPopup* create(EditorUI* editor);
    ~VideoPopup();

protected:
    // Lectura del video (ffmpeg / Media Foundation / GIF / carpeta) en un hilo aparte
    struct LoadJob {
        vx::DecodeRequest req;
        vx::Progress prog;
        vx::Clip clip;
        int shots = 0;
        std::string error;
        int id = 0;
        std::atomic<bool> done{false};
    };
    // Vista previa (un frame) en un hilo aparte
    struct PreviewJob {
        std::shared_ptr<const vx::Clip> clip;
        vx::Params params;
        vx::Result result;
        int id = 0;
        std::atomic<bool> done{false};
    };
    // Codificacion completa (al pulsar Colocar)
    struct BuildJob {
        std::shared_ptr<const vx::Clip> clip;
        vx::Params params;
        bool test = false;
        int fps = 30;
        vx::Progress prog;
        vx::Output out;
        std::atomic<bool> done{false};
    };

    bool init(EditorUI* editor);

    void onPick(CCObject*);
    void onPickFolder(CCObject*);
    void onPlace(CCObject*);
    void onTest(CCObject*);
    void onInfo(CCObject*);
    void onSettings(CCObject*);
    void onPrev(CCObject*) { stepMethod(-1); }
    void onNext(CCObject*) { stepMethod(+1); }
    void onBudgetSlider(CCObject* sender);
    void onSecondsSlider(CCObject* sender);
    void on12(CCObject*);
    void on30(CCObject*);
    void syncFpsChecks(float = 0.f);
    void onGray(CCObject*);
    void onChan(CCObject*);

    void stepMethod(int dir);
    void refreshLabels();
    void load(std::filesystem::path const& path);
    void scheduleReload();
    void reload(float = 0.f);
    void schedulePreview();
    void recomputePreview(float = 0.f);
    void tick(float);
    void applyPreview(std::shared_ptr<PreviewJob> job);
    void finishBuild(std::shared_ptr<BuildJob> job);
    void buildPreviewSprite();
    void setStatus(std::string const& a, std::string const& b = "", std::string const& note = "");

    vx::Params makeParams(int fps) const;
    int fps() const { return m_fps30 ? 30 : 12; }
    int editorLimit() const;
    int editorRoom() const;
    void startPlacement(std::vector<std::string> chunks, size_t objectCount, std::string const& doneMsg);
    CCPoint viewCenter() const;
    int maxUsedGroup() const;
    std::vector<int> freeChannels() const;

    EditorUI* m_editor = nullptr;
    std::filesystem::path m_path;
    std::shared_ptr<LoadJob> m_load;
    std::shared_ptr<PreviewJob> m_prev;
    std::shared_ptr<BuildJob> m_build;
    std::shared_ptr<const vx::Clip> m_clip;
    int m_shots = 0;
    int m_jobCounter = 0;
    vx::Result m_result;
    bool m_hasResult = false;

    int m_method = int(vx::Coder::Auto);   // 0..7 (el 8 = director automatico)
    int m_budget = 300000;                 // objetos totales
    int m_seconds = 10;                    // segundos de video a convertir
    bool m_fps30 = true;                   // check 30 fps (excluyente con 12 fps)
    bool m_gray = false;
    bool m_chan = false;

    CCNode* m_previewHolder = nullptr;
    CCSprite* m_preview = nullptr;
    CCLabelBMFont* m_statA = nullptr;
    CCLabelBMFont* m_statB = nullptr;
    CCLabelBMFont* m_noteLabel = nullptr;
    CCLabelBMFont* m_methodLabel = nullptr;
    CCLabelBMFont* m_descLabel = nullptr;
    CCLabelBMFont* m_budgetValue = nullptr;
    CCLabelBMFont* m_secondsValue = nullptr;
    Slider* m_budgetSlider = nullptr;
    Slider* m_secondsSlider = nullptr;
    CCMenuItemToggler* m_chk12 = nullptr;
    CCMenuItemToggler* m_chk30 = nullptr;
    CCMenuItemToggler* m_chkGray = nullptr;
    CCMenuItemToggler* m_chkChan = nullptr;
};
