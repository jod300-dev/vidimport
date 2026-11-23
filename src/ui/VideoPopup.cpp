#include "VideoPopup.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

#include "FitText.hpp"
#include "Layout.hpp"

namespace wlay = gdx::layout;

namespace {
    constexpr float kPopupW = wlay::kW, kPopupH = wlay::kH;
    constexpr float kPrevSize = wlay::kPrevSize;
    constexpr int kEditorObjectMargin = 3000;                       // colchon para lo que el nivel ya tenga o lo que GD anada
    constexpr int kMinSize = 150, kMaxSize = 6000;
    constexpr int kAbsMinTotal = 10000;
    constexpr double kMaxLengthSec = 500.0;                         // tope del deslizador de duracion (s)
    constexpr int kHardMinPerFrame = 40;                            // suelo ABSOLUTO del deslizador de duracion (calidad muy baja, pero cabe)
    constexpr int kMaxGroup = 9999;

    // Metodos que ofrece la ventana (los de "imagen" del motor: el video los aplica frame a frame). El primero es el recomendado.
    constexpr gdx::Method kMethods[] = {
        gdx::Method::BSP, gdx::Method::Hybrid, gdx::Method::Quadtree, gdx::Method::Pyramid,
        gdx::Method::Regions, gdx::Method::Runs, gdx::Method::Geometrize, gdx::Method::UniformGrid,
    };
    constexpr int kMethodCount = int(sizeof(kMethods) / sizeof(kMethods[0]));
    constexpr char const* kMethodDesc[kMethodCount] = {
        "RECOMENDADO. Cortes optimos en X o Y: rectangulos de cualquier forma. El mas rapido y el mas nitido en video.",
        "BSP + rectangulos girados donde queda error. Mas lento: para clips cortos con pocos frames y muchos objetos.",
        "Divide en 4 solo donde hay detalle. Rapido; bloques cuadrados (aspecto pixelado).",
        "Bloque grande y capas de detalle solo donde el color cambia. Ahorra objetos en fondos lisos.",
        "Reduce la paleta y fusiona zonas iguales. Para animacion, dibujos y videos con colores planos.",
        "Tiras de color por filas con tolerancia. Estilo escaneo, muy rapido.",
        "Solo rectangulos girados (estilo Primitive). Aspecto pictorico; lento.",
        "Una celda por objeto con el color medio. Ideal para pixel art.",
    };

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Normal") return 1;
        if (q == "Alto") return 2;
        if (q == "Maximo") return 3;
        return 0;
    }
    using gdx::fit::fitWrapped;
    // Avisos: los cortos en un FLAlertLayer; los largos (errores con mucho texto) en una ventana con SCROLL y boton OK siempre visible.
    void showAlert(char const* title, std::string const& text) {
        if (text.size() <= 180) FLAlertLayer::create(title, text.c_str(), "OK")->show();
        else MDPopup::create(title, text, "OK")->show();
    }
    std::string baseName(std::string const& p) {
        size_t i = p.find_last_of("/\\");
        return i == std::string::npos ? p : p.substr(i + 1);
    }

    // ---- deslizadores 0..1 <-> valor
    int totalMin(int maxV) { return std::min(kAbsMinTotal, std::max(1000, maxV / 2)); }
    float totalToSlider(int v, int maxV) { return std::clamp(float(v - totalMin(maxV)) / float(std::max(1, maxV - totalMin(maxV))), 0.f, 1.f); }
    int sliderToTotal(float s, int maxV) {
        const int mn = totalMin(maxV);
        const double u = mn + double(std::clamp(s, 0.f, 1.f)) * (maxV - mn);
        const int step = maxV >= 100000 ? 5000 : 500;
        return std::clamp(int(std::lround(u / step)) * step, mn, maxV);
    }
    float sizeToSlider(int u) { return std::clamp(float(std::log(double(u) / kMinSize) / std::log(double(kMaxSize) / kMinSize)), 0.f, 1.f); }
    int sliderToSize(float v) {
        double u = kMinSize * std::pow(double(kMaxSize) / kMinSize, std::clamp(v, 0.f, 1.f));
        int step = u < 600 ? 10 : u < 2000 ? 50 : 100;
        return std::clamp(int(std::lround(u / step)) * step, kMinSize, kMaxSize);
    }
    float rangeToSlider(double v, double lo, double hi) { return hi > lo ? std::clamp(float((v - lo) / (hi - lo)), 0.f, 1.f) : 0.f; }
    // paso del deslizador de duracion: fino en rangos cortos, grueso en los largos (si no, arrastrar 5 minutos seria imposible)
    double lenStep(double range) { return range > 300 ? 1.0 : range > 100 ? 0.5 : range > 30 ? 0.25 : 0.05; }
    double sliderToRange(float s, double lo, double hi, double step) {
        const double u = lo + double(std::clamp(s, 0.f, 1.f)) * (hi - lo);
        return std::clamp(std::round(u / step) * step, lo, std::max(lo, hi));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Colocacion POR TANDAS. Crear 100k-500k objetos de golpe con createObjectsFromString congela el juego (o lo cierra en moviles) y,
// si se pasa el limite de objetos del editor, GD corta la cadena y se pierden los ultimos objetos. Aqui la cadena se parte en trozos
// de ~1500 objetos (cortando siempre en ';') y se crean unos pocos por fotograma (~25 ms), con aviso de progreso.
// El nodo cuelga del editor: si se sale del editor, la colocacion se cancela sola.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
    std::vector<std::string> splitChain(std::string const& s, size_t perPart) {
        std::vector<std::string> parts;
        size_t start = 0, count = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ';') {
                if (i > start) ++count;
                if (count >= perPart || i == s.size()) {
                    if (i > start || !parts.empty()) {
                        size_t endPos = i;
                        while (endPos > start && s[endPos - 1] == ';') --endPos;
                        if (endPos > start) parts.push_back(s.substr(start, endPos - start));
                    }
                    start = i + 1; count = 0;
                }
            }
        }
        return parts;
    }

    class PlaceRunner : public CCNode {
    public:
        std::vector<std::string> parts;
        size_t next = 0, placed = 0, expected = 0, totalObjs = 0;
        bool selectAfter = false;
        std::string doneMsg;
        Ref<CCLabelBMFont> label;
        CCArray* selected = nullptr;

        static PlaceRunner* create() {
            auto r = new PlaceRunner();
            if (r && r->init()) { r->autorelease(); return r; }
            delete r; return nullptr;
        }
        ~PlaceRunner() {
            if (selected) selected->release();
            if (label && label->getParent()) label->removeFromParent();
        }

        void begin(std::vector<std::string> p, size_t total, bool select, std::string const& msg) {
            parts = std::move(p); totalObjs = total; doneMsg = msg;
            selectAfter = select && total <= 8000;                    // seleccionar decenas de miles de objetos tambien cuelga el editor
            if (selectAfter) { selected = CCArray::create(); selected->retain(); }
            if (auto scene = CCDirector::sharedDirector()->getRunningScene()) {
                label = CCLabelBMFont::create("Colocando objetos... 0%", "bigFont.fnt");
                auto ws = CCDirector::sharedDirector()->getWinSize();
                label->setScale(0.55f);
                label->setPosition({ws.width / 2.f, ws.height - 28.f});
                scene->addChild(label, 9999);
            }
            this->schedule(schedule_selector(PlaceRunner::step));
        }

        void step(float) {
            auto lel = LevelEditorLayer::get();
            if (!lel) { this->finish(false); return; }
            auto t0 = std::chrono::steady_clock::now();
            do {
                if (next >= parts.size()) break;
                {
                    size_t segs = parts[next].empty() ? 0 : 1;
                    for (char ch : parts[next]) if (ch == ';') ++segs;
                    expected += segs;
                }
                CCArray* made = lel->createObjectsFromString(parts[next].c_str(), true, true);
                if (made) {
                    placed += made->count();
                    if (selected) for (auto* o : CCArrayExt<GameObject*>(made)) selected->addObject(o);
                }
                std::string().swap(parts[next]);
                ++next;
            } while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 25.0);
            if (next >= parts.size()) { this->finish(true); return; }
            char buf[96];
            std::snprintf(buf, sizeof buf, "Colocando objetos... %d%%", int(100.0 * double(next) / double(std::max<size_t>(1, parts.size()))));
            if (label) label->setString(buf);
        }

        void finish(bool ok) {
            this->unschedule(schedule_selector(PlaceRunner::step));
            if (ok && selected && selected->count() > 0) {
                if (auto* ed = EditorUI::get()) ed->selectObjects(selected, true);
            }
            if (label && label->getParent()) label->removeFromParent();
            std::string msg = ok ? doneMsg : std::string("Colocacion cancelada (se salio del editor)");
            bool warn = false;
            if (ok && placed < expected) {                       // GD no creo todo lo que se le pidio: la animacion saldria con huecos
                msg += " | AVISO: GD creo " + std::to_string(placed) + " de " + std::to_string(expected) +
                       " objetos. Tu editor tiene un limite de objetos: instala un mod que lo quite o baja el tope total.";
                warn = true;
            }
            Notification::create(msg, ok && !warn ? NotificationIcon::Success : NotificationIcon::Error, warn ? 10.f : 4.f)->show();
            this->removeFromParent();
        }
    };
}

// ------------------------------------------------------------------------------------------------------- ventana ----

VideoPopup* VideoPopup::create(EditorUI* editor) {
    auto ret = new VideoPopup();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

VideoPopup::~VideoPopup() {
    if (m_job) m_job->prog.cancel.store(true);
    if (m_build) { m_build->prog.cancel.store(true); m_build->cancelDecode.store(true); }
}

gdx::Method VideoPopup::method() const { return kMethods[std::clamp(m_method, 0, kMethodCount - 1)]; }

bool VideoPopup::init(EditorUI* editor) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_editor = editor;
    this->setTitle("Video Importer (beta)");

    auto* mod = Mod::get();
    const int settingMax = std::clamp(int(mod->getSettingValue<int>("max-total-objects")), 5000, 1000000);
    m_method = std::clamp(mod->getSavedValue<int>("method", 0), 0, kMethodCount - 1);
    m_total = std::clamp(mod->getSavedValue<int>("total", settingMax), totalMin(settingMax), settingMax);
    m_sizeU = std::clamp(mod->getSavedValue<int>("size-u", 600), kMinSize, kMaxSize);
    m_fps = mod->getSavedValue<int>("fps", 12) == 30 ? 30 : 12;
    {
        const int sm = mod->getSavedValue<int>("mode", 0);
        m_mode = sm == 1 ? gdx::PlayMode::Spawn : sm == 2 ? gdx::PlayMode::Remap : gdx::PlayMode::Toggle;
    }
    m_gray = mod->getSavedValue<bool>("gray", false);
    m_chan = mod->getSavedValue<bool>("chan", false);
    m_lag = mod->getSavedValue<bool>("lag", false);
    m_groupBase = std::max(this->maxUsedGroup() + 1, std::clamp(int(mod->getSettingValue<int>("first-group")), 1, 9000));

    auto put = [&](CCNode* n, float x, float y) { m_mainLayer->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto putBtn = [&](CCNode* n, float x, float y) { m_buttonMenu->addChildAtPosition(n, Anchor::TopLeft, ccp(x, -y)); };
    auto label = [&](char const* text, char const* font, float scale, float x, float y, float ax = 0.5f) {
        auto l = CCLabelBMFont::create(text, font);
        l->setScale(scale);
        l->setAnchorPoint({ax, 0.5f});
        put(l, x, y);
        return l;
    };

    // ============ izquierda: vista previa y estado ============
    m_previewHolder = CCNode::create();
    m_previewHolder->setContentSize({kPrevSize, kPrevSize});
    m_previewHolder->setAnchorPoint({0.5f, 0.5f});
    m_previewHolder->addChild(CCLayerColor::create(ccc4(0, 0, 0, 120), kPrevSize, kPrevSize));
    auto hint = CCLabelBMFont::create("Elige un MP4", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    put(m_previewHolder, wlay::kLeftCx, wlay::kPrevCy);

    m_statA = label("Elige un video MP4", "chatFont.fnt", 0.85f, wlay::kLeftCx, wlay::kStatACy);
    m_statB = label("", "chatFont.fnt", 0.75f, wlay::kLeftCx, wlay::kStatBCy);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", wlay::kNoteMaxW, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    put(m_noteLabel, wlay::kLeftCx, wlay::kNoteCy);

    // ============ derecha ============
    auto arrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        putBtn(CCMenuItemSpriteExtra::create(spr, this, cb), x, y);
    };
    arrow(false, menu_selector(VideoPopup::onPrev), wlay::kArrowCxL, wlay::kArrowCy);
    arrow(true,  menu_selector(VideoPopup::onNext), wlay::kArrowCxR, wlay::kArrowCy);
    m_methodLabel = label("", "bigFont.fnt", 0.5f, wlay::kRightCx, wlay::kArrowCy);
    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", wlay::kDescMaxW, kCCTextAlignmentCenter);
    m_descLabel->setScale(0.55f);
    put(m_descLabel, wlay::kRightCx, wlay::kDescCy);

    static char const* kSliderNames[4] = {"Objetos totales", "Lado largo", "Inicio", "Duracion"};
    Slider** sliders[4] = {&m_totalSlider, &m_sizeSlider, &m_startSlider, &m_lenSlider};
    SEL_MenuHandler cbs[4] = {menu_selector(VideoPopup::onTotalSlider), menu_selector(VideoPopup::onSizeSlider),
                              menu_selector(VideoPopup::onStartSlider), menu_selector(VideoPopup::onLenSlider)};
    for (int i = 0; i < 4; ++i) {
        const float y = wlay::kSliderLabelY[i];
        label(kSliderNames[i], "bigFont.fnt", 0.32f, wlay::kRightL, y, 0.f);
        m_val[i] = label("", "bigFont.fnt", 0.32f, wlay::kRightR, y, 1.f);
        *sliders[i] = Slider::create(this, cbs[i], 0.78f);
        put(*sliders[i], wlay::kRightCx, y + wlay::kSliderOffset);
    }
    m_totalSlider->setValue(totalToSlider(m_total, settingMax));
    m_sizeSlider->setValue(sizeToSlider(m_sizeU));
    m_startSlider->setValue(0.f);
    m_lenSlider->setValue(1.f);

    // checks (fila A: fps y grises; fila B: modo de reproduccion). 12/30 y Toggle/Spawn/Remap son excluyentes (ver syncChecks)
    auto chk = [&](SEL_MenuHandler cb, bool on, float x, float y) {
        auto c = CCMenuItemToggler::createWithStandardSprites(this, cb, wlay::kChkSize);
        c->toggle(on);
        putBtn(c, x, y);
        return c;
    };
    auto chkLabel = [&](char const* text, float x, float y, float maxW) {
        auto l = label(text, "bigFont.fnt", 0.28f, x, y, 0.f);
        l->limitLabelWidth(maxW, 0.28f, 0.15f);
    };
    m_chk12 = chk(menu_selector(VideoPopup::onFps12), m_fps == 12, 174.f, wlay::kChkRowA);
    chkLabel("12 fps", 186.f, wlay::kChkRowA, 42.f);
    m_chk30 = chk(menu_selector(VideoPopup::onFps30), m_fps == 30, 234.f, wlay::kChkRowA);
    chkLabel("30 fps", 246.f, wlay::kChkRowA, 42.f);
    m_chkGray = chk(menu_selector(VideoPopup::onGray), m_gray, 292.f, wlay::kChkRowA);
    chkLabel("Grises", 304.f, wlay::kChkRowA, 40.f);
    m_chkChan = chk(menu_selector(VideoPopup::onChan), m_chan, 348.f, wlay::kChkRowA);
    chkLabel("Canales", 360.f, wlay::kChkRowA, 56.f);
    m_chkToggle = chk(menu_selector(VideoPopup::onModeToggle), m_mode == gdx::PlayMode::Toggle, 174.f, wlay::kChkRowB);
    chkLabel("Toggle (exacto)", 186.f, wlay::kChkRowB, 74.f);
    m_chkSpawn = chk(menu_selector(VideoPopup::onModeSpawn), m_mode == gdx::PlayMode::Spawn, 262.f, wlay::kChkRowB);
    chkLabel("Spawn (ligero)", 274.f, wlay::kChkRowB, 70.f);
    m_chkRemap = chk(menu_selector(VideoPopup::onModeRemap), m_mode == gdx::PlayMode::Remap, 346.f, wlay::kChkRowB);
    chkLabel("Remap (mini)", 358.f, wlay::kChkRowB, 64.f);
    m_chkLag = chk(menu_selector(VideoPopup::onLag), m_lag, 174.f, wlay::kChkRowC);
    chkLabel("Partes anti-lag (Move)", 186.f, wlay::kChkRowC, 150.f);

    // ---- fila de botones: UNA sola fila (misma Y, misma altura, misma escala de texto)
    auto mkBtn = [&](char const* text, char const* frame, SEL_MenuHandler cb) {
        return CCMenuItemSpriteExtra::create(ButtonSprite::create(text, "bigFont.fnt", frame, wlay::kBtnTextScale), this, cb);
    };
    auto putLeft = [&](CCMenuItemSpriteExtra* b, float left) {
        const float w = b->getContentSize().width;
        putBtn(b, wlay::centerFromLeft(left, w), wlay::kBtnRowY);
        return left + w;
    };
    auto putRight = [&](CCMenuItemSpriteExtra* b, float right) {
        const float w = b->getContentSize().width;
        putBtn(b, wlay::centerFromRight(right, w), wlay::kBtnRowY);
        return right - w;
    };
    putLeft(mkBtn("Ajustes", "GJ_button_04.png", menu_selector(VideoPopup::onSettings)), wlay::kLeftL);
    auto pickBtn = mkBtn("Video", "GJ_button_01.png", menu_selector(VideoPopup::onPick));
    auto placeBtn = mkBtn("Colocar", "GJ_button_02.png", menu_selector(VideoPopup::onPlace));
    putRight(pickBtn, putRight(placeBtn, wlay::kRightR) - wlay::kBtnGap);

    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(VideoPopup::onInfo));
    putBtn(infoBtn, wlay::kInfoCx, wlay::kInfoCy);

    this->schedule(schedule_selector(VideoPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

int VideoPopup::framesNow() const { return m_haveVideo ? vid::framesFor(m_length, m_fps) : 0; }

int VideoPopup::editorRoom() const {
    auto lel = LevelEditorLayer::get();
    const int cur = (lel && lel->m_objects) ? int(lel->m_objects->count()) : 0;
    const int lim = std::clamp(int(Mod::get()->getSettingValue<int>("editor-object-limit")), 50000, 1000000);
    return std::max(0, lim - kEditorObjectMargin - cur);
}

int VideoPopup::paletteSize() const {
    return m_chan ? std::clamp(int(Mod::get()->getSettingValue<int>("palette-size")), 8, gdx::kVideoMaxPalette) : 0;
}

int VideoPopup::maxTotal() const { return std::clamp(std::min(m_total, editorRoom()), 500, 1000000); }

// Frames que caben con `perFrame` objetos cada uno (6 = cota de triggers por frame) y sin pasarse de los 9999 grupos de GD
// (hacen falta ~2 por frame: uno para sus objetos y otro para su paso de triggers).
// Modo Remap: ~1 trigger y ~1 grupo por frame (la plantilla de Toggle se reutiliza); el anti-lag suma 4 triggers y 3 grupos por PARTE (pocas).
static int framesThatFit(int total, int palette, int perFrame, int groupBase, bool lag, bool remap = false) {
    if (remap) {
        const int chunks = lag ? total / 25000 + 2 : 0;                                              // cota de partes anti-lag
        const int byObjects = std::max(1, (total - 40 - palette - 4 * chunks) / (perFrame + 1));      // 1 Spawn por frame
        const int byGroups = std::max(1, kMaxGroup - groupBase - 12 - 3 * chunks);                   // ~1 grupo por frame
        return std::min(byObjects, byGroups);
    }
    const int byObjects = std::max(1, (total - 16 - palette) / (perFrame + (lag ? 8 : 6)));      // 6 triggers por frame (+2 Move con anti-lag)
    const int byGroups = std::max(1, (kMaxGroup - groupBase - 4) / 2);
    return std::min(byObjects, byGroups);
}

// Duracion MAXIMA del deslizador (suelo absoluto de 40 obj/frame): se puede llegar lejos, aunque con calidad baja.
double VideoPopup::maxLengthFit() const {
    return std::min(kMaxLengthSec, double(framesThatFit(maxTotal(), paletteSize(), kHardMinPerFrame, m_groupBase, m_lag, m_mode == gdx::PlayMode::Remap)) / double(m_fps));
}

// Duracion RECOMENDADA (con el minimo por frame de Ajustes): la que se propone al abrir un video.
double VideoPopup::softLengthFit() const {
    const int minPer = std::clamp(int(Mod::get()->getSettingValue<int>("min-per-frame")), 30, 3000);
    return std::min(kMaxLengthSec, double(framesThatFit(maxTotal(), paletteSize(), minPer, m_groupBase, m_lag, m_mode == gdx::PlayMode::Remap)) / double(m_fps));
}

int VideoPopup::perFrameEstimate() const {
    const int n = std::max(1, framesNow());
    if (m_mode == gdx::PlayMode::Remap)
        return std::max(0, int((long(maxTotal()) - gdx::videoTriggerBoundRemap(n, n, m_lag ? long(maxTotal() / 25000) + 2 : 0L) - paletteSize()) / n));
    return std::max(0, int((long(maxTotal()) - gdx::videoTriggerBound(n, n) - (m_lag ? 2L * n : 0L) - paletteSize()) / n));
}

int VideoPopup::storeLong(int frames) const {
    const int cap = std::clamp(int(Mod::get()->getSettingValue<int>("max-raster")), 160, 1024);
    if (!m_haveVideo || frames <= 0) return cap;
    const double a = std::max(1e-3, double(m_info.displayW) / std::max(1, m_info.displayH));
    const double budgetBytes = 600.0 * 1024 * 1024;                       // memoria maxima de los frames decodificados
    const double L = std::sqrt(budgetBytes / (double(frames) * 4.0) * std::max(a, 1.0 / a));
    return std::clamp(int(std::min<double>(cap, L)), 160, cap);
}

void VideoPopup::clampClip() {
    if (!m_haveVideo) return;
    const double dur = m_info.duration;
    m_start = std::clamp(m_start, 0.0, std::max(0.0, dur - 0.5));
    double maxLen = std::min(dur - m_start, maxLengthFit());
    const double minLen = std::min(0.5, std::max(0.1, maxLen));
    maxLen = std::max(maxLen, minLen);
    m_length = std::clamp(std::floor(m_length * 20.0 + 1e-6) / 20.0, minLen, maxLen);
}

void VideoPopup::syncSliders() {
    auto* mod = Mod::get();
    const int settingMax = std::clamp(int(mod->getSettingValue<int>("max-total-objects")), 5000, 1000000);
    m_totalSlider->setValue(totalToSlider(m_total, settingMax));
    if (!m_haveVideo) return;
    const double dur = m_info.duration;
    m_startSlider->setValue(rangeToSlider(m_start, 0.0, std::max(0.1, dur - 0.5)));
    const double maxLen = std::max(0.5, std::min(dur - m_start, maxLengthFit()));
    m_lenSlider->setValue(rangeToSlider(m_length, std::min(0.5, maxLen), maxLen));
}

void VideoPopup::refreshLabels() {
    auto const& info = gdx::methodInfo(method());
    m_methodLabel->setString(info.name);
    m_methodLabel->limitLabelWidth(wlay::kTitleMaxW, 0.5f, 0.2f);
    const std::string desc = kMethodDesc[std::clamp(m_method, 0, kMethodCount - 1)];
    if (desc != m_lastDesc) {
        m_lastDesc = desc;
        fitWrapped(m_descLabel, desc, wlay::kDescMaxW, wlay::kDescMaxH, 0.55f, 0.3f);
    }

    char buf[96];
    if (maxTotal() < m_total) std::snprintf(buf, sizeof buf, "%d (editor: %d)", m_total, maxTotal());
    else std::snprintf(buf, sizeof buf, "%d", m_total);
    m_val[0]->setString(buf);
    m_val[0]->limitLabelWidth(120.f, 0.32f, 0.18f);
    std::snprintf(buf, sizeof buf, "%d u (%.1f bloques)", m_sizeU, m_sizeU / 30.0);
    m_val[1]->setString(buf);
    if (m_haveVideo) {
        std::snprintf(buf, sizeof buf, "%.1f s de %.1f", m_start, m_info.duration);
        m_val[2]->setString(buf);
        const int n = framesNow();
        const double fit = maxLengthFit();
        if (m_length >= fit - 1e-6 && fit < m_info.duration - m_start - 1e-6) std::snprintf(buf, sizeof buf, "%.1f s (%d fr, max)", m_length, n);
        else std::snprintf(buf, sizeof buf, "%.2f s (%d frames)", m_length, n);
        m_val[3]->setString(buf);
    } else {
        m_val[2]->setString("-"); m_val[3]->setString("-");
    }
    m_val[2]->limitLabelWidth(120.f, 0.32f, 0.18f);
    m_val[3]->limitLabelWidth(130.f, 0.32f, 0.18f);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("method", m_method);
    mod->setSavedValue<int>("total", m_total);
    mod->setSavedValue<int>("size-u", m_sizeU);
    mod->setSavedValue<int>("fps", m_fps);
    mod->setSavedValue<int>("mode", int(m_mode));
    mod->setSavedValue<bool>("gray", m_gray);
    mod->setSavedValue<bool>("chan", m_chan);
    mod->setSavedValue<bool>("lag", m_lag);
}

void VideoPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(150.f, 0.85f, 0.4f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(150.f, 0.75f, 0.4f);
    fitWrapped(m_noteLabel, note, wlay::kNoteMaxW, wlay::kNoteMaxH, 0.6f, 0.4f);
}

// ---------------------------------------------------------------- callbacks --

void VideoPopup::stepMethod(int dir) {
    m_method = (m_method + dir + kMethodCount) % kMethodCount;
    this->refreshLabels();
    this->scheduleRecompute();
}

void VideoPopup::onTotalSlider(CCObject* sender) {
    const int settingMax = std::clamp(int(Mod::get()->getSettingValue<int>("max-total-objects")), 5000, 1000000);
    const int v = sliderToTotal(static_cast<SliderThumb*>(sender)->getValue(), settingMax);
    if (v == m_total) return;
    m_total = v;
    this->clampClip();                         // mas objetos = cabe mas duracion
    this->syncSliders();
    this->refreshLabels();
    this->scheduleRecompute();
}

void VideoPopup::onSizeSlider(CCObject* sender) {
    m_sizeU = sliderToSize(static_cast<SliderThumb*>(sender)->getValue());
    this->refreshLabels();                     // solo afecta a la colocacion
}

void VideoPopup::onStartSlider(CCObject* sender) {
    if (!m_haveVideo) return;
    m_start = sliderToRange(static_cast<SliderThumb*>(sender)->getValue(), 0.0, std::max(0.1, m_info.duration - 0.5), 0.1);
    this->clampClip();
    this->syncSliders();
    this->refreshLabels();
    this->scheduleRecompute();                 // la vista previa es el frame del instante de inicio
}

void VideoPopup::onLenSlider(CCObject* sender) {
    if (!m_haveVideo) return;
    const double maxLen = std::max(0.5, std::min(m_info.duration - m_start, maxLengthFit()));
    m_length = sliderToRange(static_cast<SliderThumb*>(sender)->getValue(), std::min(0.5, maxLen), maxLen, lenStep(maxLen));
    this->clampClip();
    this->refreshLabels();
    if (m_hasResult && !m_build) this->applyPreview(nullptr);
}

void VideoPopup::onGray(CCObject*) {
    m_gray = !m_gray;                          // en el callback el toggler aun no ha cambiado su estado
    this->refreshLabels();
    this->scheduleRecompute();
}

// Check "Canales (sin HSV)": los objetos usan solo canales de color (2..), con UNA paleta global y Color triggers al inicio.
void VideoPopup::onChan(CCObject*) {
    m_chan = !m_chan;                          // en el callback el toggler aun no ha cambiado su estado
    this->scheduleOnce(schedule_selector(VideoPopup::syncChecks), 0.f);
    this->clampClip();                         // la paleta resta objetos al presupuesto
    this->syncSliders();
    this->refreshLabels();
    if (m_hasResult && !m_build) this->applyPreview(nullptr);
}

// Check "Partes anti-lag": los frames se reparten en partes de ~N objetos; solo una esta cerca de la camara (el resto espera lejos).
void VideoPopup::onLag(CCObject*) {
    m_lag = !m_lag;                            // en el callback el toggler aun no ha cambiado su estado
    this->scheduleOnce(schedule_selector(VideoPopup::syncChecks), 0.f);
    this->clampClip();                         // los Move cuentan como triggers del presupuesto
    this->syncSliders();
    this->refreshLabels();
    if (m_hasResult && !m_build) this->applyPreview(nullptr);
}

void VideoPopup::setFps(int fps) {
    m_fps = fps;
    this->scheduleOnce(schedule_selector(VideoPopup::syncChecks), 0.f);
    this->clampClip();
    this->syncSliders();
    this->refreshLabels();
    this->scheduleRecompute();
}

void VideoPopup::setMode(gdx::PlayMode m) {
    m_mode = m;                                // Remap usa ~1 grupo y ~1 trigger por frame: cambia hasta donde llega la duracion
    this->scheduleOnce(schedule_selector(VideoPopup::syncChecks), 0.f);
    this->clampClip();
    this->syncSliders();
    this->refreshLabels();
    this->scheduleRecompute();
}

// El CCMenuItemToggler cambia su estado DESPUES de llamar al callback: sin esto, pulsar la casilla ya marcada la dejaria desmarcada
// y las dos opciones sin elegir. Un fotograma despues se fuerza el estado real.
void VideoPopup::syncChecks(float) {
    m_chk12->toggle(m_fps == 12);
    m_chk30->toggle(m_fps == 30);
    m_chkToggle->toggle(m_mode == gdx::PlayMode::Toggle);
    m_chkSpawn->toggle(m_mode == gdx::PlayMode::Spawn);
    m_chkRemap->toggle(m_mode == gdx::PlayMode::Remap);
    m_chkGray->toggle(m_gray);
    m_chkChan->toggle(m_chan);
    m_chkLag->toggle(m_lag);
}

void VideoPopup::onSettings(CCObject*) { openSettingsPopup(Mod::get()); }

// ---------------------------------------------------------------- elegir video --

void VideoPopup::onPick(CCObject*) {
    if (m_build) return;
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"Video MP4", {"*.mp4", "*.m4v", "*.mov"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<VideoPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) {
                self->setStatus("No se pudo abrir", "el selector de archivos");
                return;
            }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            self->loadVideo(utils::string::pathToString(opt.value()));
        }
    );
}

void VideoPopup::loadVideo(std::string const& path) {
    m_hasResult = false;
    auto info = vid::probe(path);
    if (!info.ok) {
        m_haveVideo = false;
        this->setStatus("Video no valido", "", info.error);
        return;
    }
    m_path = path;
    m_info = info;
    m_haveVideo = true;
    m_loadNote.clear();
    if (!m_info.decodable())
        m_loadNote = "Codec " + m_info.codecName() + ": tu sistema puede no decodificarlo (H.264 siempre funciona).";
    else if (m_info.fps > 0 && m_fps == 30 && m_info.fps < 20)
        m_loadNote = "El video va a ~" + std::to_string(int(std::lround(m_info.fps))) + " fps: a 30 fps se repetiran frames (usa 12).";
    m_start = 0;
    m_length = std::min(m_info.duration, std::max(0.5, softLengthFit()));     // por defecto la que se ve bien; el deslizador llega mas lejos
    this->clampClip();
    this->syncSliders();
    this->refreshLabels();
    this->recompute();
}

// ----------------------------------------------------------------- calculo --

gdx::Options VideoPopup::makeOptions(int budget) const {
    gdx::Options o;
    auto* mod = Mod::get();
    o.budget = std::max(1, budget);
    o.maxRaster = std::clamp(int(mod->getSettingValue<int>("max-raster")), 160, 1024);
    o.alphaThreshold = 128;
    o.quality = qualityFromSetting();
    o.colorChannels = false;           // sin Color triggers: el color va en HSV dentro de cada objeto
    o.mainChannels = false;
    o.lightSpecialChannels = true;
    o.grayscale = m_gray;
    o.maxChannels = 0;
    return o;
}

gdx::GdExportOptions VideoPopup::makeExportOptions() const {
    auto* mod = Mod::get();
    gdx::GdExportOptions eo;
    eo.longSide = m_sizeU;
    eo.colorChannel = std::clamp(int(mod->getSettingValue<int>("color-channel")), 1, 1020);
    eo.bleed = std::clamp(mod->getSettingValue<double>("bleed"), 0.0, 2.0);
    eo.flipRotation = mod->getSettingValue<bool>("flip-rotation");
    return eo;
}

gdx::VideoPlanOptions VideoPopup::makePlanOptions() const {
    auto* mod = Mod::get();
    gdx::VideoPlanOptions po;
    po.maxTotal = maxTotal();
    po.minPerFrame = std::clamp(int(mod->getSettingValue<int>("min-per-frame")), 30, 3000);
    po.dedupe = mod->getSettingValue<bool>("dedupe");
    po.dedupePsnr = std::clamp(mod->getSettingValue<double>("dedupe-psnr"), 30.0, 60.0);
    po.adaptive = mod->getSettingValue<bool>("adaptive");
    po.stabilizeTol = std::clamp(int(mod->getSettingValue<int>("stabilize")), 0, 20);
    po.extraObjects = paletteSize();           // los Color triggers de la paleta cuentan como objetos
    po.lagMoves = m_lag;                       // 2 Move triggers por grupo de frames (Remap: 4 por parte)
    po.remap = m_mode == gdx::PlayMode::Remap;
    po.lagChunkObjects = std::clamp(int(mod->getSettingValue<int>("lag-chunk-objects")), 500, 200000);
    return po;
}

void VideoPopup::scheduleRecompute() {
    if (!m_haveVideo || m_build) return;
    this->unschedule(schedule_selector(VideoPopup::recompute));
    this->scheduleOnce(schedule_selector(VideoPopup::recompute), 0.35f);
}

void VideoPopup::recompute(float) {
    if (!m_haveVideo || m_build) return;
    if (m_job) m_job->prog.cancel.store(true);
    auto* mod = Mod::get();
    auto job = std::make_shared<PreviewJob>();
    job->id = ++m_jobCounter;
    job->path = m_path;
    job->info = m_info;
    job->start = m_start;
    job->fps = m_fps;
    job->storeLong = storeLong(std::max(1, framesNow()));
    job->ffmpegPath = mod->getSettingValue<std::string>("ffmpeg-path");
    job->allowFfmpeg = mod->getSettingValue<bool>("use-ffmpeg");
    job->method = method();
    job->opt = makeOptions(std::max(100, perFrameEstimate()));
    job->opt.maxRaster = std::min(job->opt.maxRaster, gdx::rasterFor(job->opt.budget, double(m_info.displayW) / std::max(1, m_info.displayH), job->storeLong));
    m_job = job;
    this->setStatus("Leyendo el video...");
    std::thread([job]() {
        try {
            vid::SampleSpec sp;
            sp.path = job->path; sp.start = job->start; sp.fps = job->fps; sp.storeLong = job->storeLong;
            sp.length = 1.5 / job->fps;                          // exactamente 1 frame
            sp.ffmpegPath = job->ffmpegPath; sp.allowFfmpeg = job->allowFfmpeg;
            auto sr = vid::sampleVideo(sp, job->info);
            if (!sr.ok()) { job->error = sr.error.empty() ? "No se pudo decodificar" : sr.error; }
            else {
                job->backend = sr.backend;
                job->result = gdx::convert(sr.frames[0], job->method, job->opt, &job->prog);
            }
        } catch (...) {
            job->error = "Error interno al calcular";
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void VideoPopup::tick(float) {
    if (m_build) {
        if (m_build->done.load(std::memory_order_acquire)) {
            auto job = m_build;
            m_build = nullptr;
            if (!job->prog.cancel.load()) this->finishBuild(job);
            return;
        }
        char buf[64];
        if (m_build->phase.load() == 0) std::snprintf(buf, sizeof buf, "Decodificando... %d%%", int(m_build->decodeProg.load() * 100.f));
        else std::snprintf(buf, sizeof buf, "Convirtiendo... %d%%", int(m_build->prog.fraction.load() * 100.f));
        this->setStatus(buf, "No cierres la ventana");
        return;
    }
    if (!m_job) return;
    if (m_job->done.load(std::memory_order_acquire)) {
        auto job = m_job;
        m_job = nullptr;
        if (job->id == m_jobCounter) this->applyPreview(job);
    }
}

void VideoPopup::applyPreview(std::shared_ptr<PreviewJob> job) {
    if (job) {
        if (!job->error.empty()) {
            m_hasResult = false;
            this->setStatus("No se pudo leer el video", "", "Mira el aviso");
            showAlert("Video Importer", job->error + "\n\nSi es un MP4 de H.264 y falla, prueba a re-exportarlo (H.264, sin HDR) o instala ffmpeg en PC (Ajustes).");
            return;
        }
        m_result = std::move(job->result);
        m_hasResult = !m_result.shapes.empty();
        if (!m_hasResult) { this->setStatus("No se genero ningun objeto", "", m_result.note); return; }
        this->buildPreview();
    }
    if (!m_hasResult || !m_haveVideo) return;
    const int n = framesNow();
    const int pf = perFrameEstimate();
    const double fit = maxLengthFit();
    char a[96], b[96];
    std::snprintf(a, sizeof a, "%d frames | %s", n, baseName(m_path).c_str());
    std::snprintf(b, sizeof b, "~%d obj/frame | %.1f dB", pf, m_result.psnr);
    std::string note = m_loadNote;
    const int minPer = std::clamp(int(Mod::get()->getSettingValue<int>("min-per-frame")), 30, 3000);
    if (pf < minPer) note = "Calidad baja (" + std::to_string(pf) + " obj/frame): acorta la duracion, usa 12 fps o sube el tope. " + note;
    else if (m_length >= fit - 1e-6 && fit < m_info.duration - m_start - 1e-6) {
        if (fit >= kMaxLengthSec - 1e-6) note = "Duracion limitada a 500 s. " + note;
        else note = std::string("Duracion limitada por el tope de objetos o de grupos de GD (9999). ") + (m_mode == gdx::PlayMode::Remap ? "Remap usa ~1 grupo por frame. " : "Con Toggle/Spawn hacen falta ~2 por frame: prueba Remap. ") + note;
    }
    this->setStatus(a, b, note);
}

void VideoPopup::buildPreview() {
    if (m_preview) { m_preview->removeFromParent(); m_preview = nullptr; }
    int w = m_result.rasterW, h = m_result.rasterH;
    if (w <= 0 || h <= 0) return;
    gdx::Image img = gdx::renderShapes(m_result.shapes, w, h);
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const uint8_t* p = &img.rgba[i * 4];
        const float al = p[3] / 255.f;
        for (int c = 0; c < 3; ++c) buf[i * 4 + c] = uint8_t(p[c] * al + 40 * (1.f - al));
        buf[i * 4 + 3] = 255;
    }
    auto tex = new CCTexture2D();
    if (!tex->initWithData(buf.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(float(w), float(h)))) {
        delete tex;
        return;
    }
    tex->autorelease();
    m_preview = CCSprite::createWithTexture(tex);
    if (!m_preview) return;
    const float aspect = m_result.aspect > 0 ? m_result.aspect : 1.f;
    const float tw = aspect >= 1.f ? kPrevSize : kPrevSize * aspect;
    const float th = aspect >= 1.f ? kPrevSize / aspect : kPrevSize;
    m_preview->setScaleX(tw / w);
    m_preview->setScaleY(th / h);
    m_preview->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(m_preview);
}

// --------------------------------------------------------------- colocar ----

void VideoPopup::startPlacement(std::string const& chain, size_t objectCount, std::string const& doneMsg) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto runner = PlaceRunner::create();
    if (!runner) return;
    lel->addChild(runner);
    runner->begin(splitChain(chain, 1500), objectCount, Mod::get()->getSettingValue<bool>("select-after"), doneMsg);
}

CCPoint VideoPopup::placementOrigin() const {
    // Con un unico objeto seleccionado, el video se centra en el; si no, en el centro de la vista del editor.
    if (m_editor) {
        auto sel = m_editor->getSelectedObjects();
        if (sel && sel->count() == 1) {
            auto obj = static_cast<GameObject*>(sel->objectAtIndex(0));
            return {obj->getPositionX(), obj->getPositionY()};
        }
    }
    auto lel = LevelEditorLayer::get();
    auto win = CCDirector::get()->getWinSize();
    if (lel && lel->m_objectLayer) return lel->m_objectLayer->convertToNodeSpace(ccp(win.width / 2, win.height / 2));
    return {0, 0};
}

// Grupo mas alto que ya usa el nivel (objetos y destinos de triggers). Escrito con "requires" para
// compilar aunque los bindings cambien la forma de m_groups.
int VideoPopup::maxUsedGroup() const {
    int mx = 0;
    auto lel = LevelEditorLayer::get();
    if (!lel || !lel->m_objects) return 0;
    auto scan = [&](auto* o) {
        if constexpr (requires { o->m_groups; o->m_groupCount; }) {
            auto* arr = o->m_groups;
            if (arr) {
                int cnt = std::min(int(o->m_groupCount), 10);
                for (int i = 0; i < cnt; ++i) {
                    int v;
                    if constexpr (requires { (*arr)[0]; }) v = int((*arr)[size_t(i)]);
                    else v = int(arr[i]);
                    mx = std::max(mx, v);
                }
            }
        }
    };
    for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
        if (!obj) continue;
        scan(obj);
        if (auto* eff = typeinfo_cast<EffectGameObject*>(obj)) {
            mx = std::max(mx, int(eff->m_targetGroupID));
            mx = std::max(mx, int(eff->m_centerGroupID));
        }
    }
    return mx;
}

void VideoPopup::onPlace(CCObject*) {
    if (m_build) return;
    if (m_job) {
        FLAlertLayer::create("Video Importer", "Todavia se esta calculando la vista previa. Espera un momento.", "OK")->show();
        return;
    }
    if (!m_haveVideo || !m_hasResult) {
        FLAlertLayer::create("Video Importer", "Primero elige un video MP4.", "OK")->show();
        return;
    }
    if (!LevelEditorLayer::get()) return;
    if (this->editorRoom() < 500) {
        FLAlertLayer::create("Video Importer", "El nivel ya esta casi en el limite de objetos del editor (ver Ajustes). No hay sitio para el video.", "OK")->show();
        return;
    }
    auto* mod = Mod::get();
    const int N = framesNow();
    if (N < 2 && !mod->getSettingValue<bool>("only-first-frame")) {
        FLAlertLayer::create("Video Importer", "El tramo elegido es demasiado corto (hacen falta al menos 2 frames).", "OK")->show();
        return;
    }

    // Prueba rapida: solo el primer frame, estatico, sin triggers
    if (mod->getSettingValue<bool>("only-first-frame")) {
        auto eo = makeExportOptions();
        const CCPoint origin = placementOrigin();
        eo.originX = origin.x; eo.originY = origin.y;
        eo.monoFastPath = false;
        std::string s = gdx::toGdString(m_result, eo);
        if (s.empty()) { showAlert("Video Importer", "No se pudo generar el frame."); return; }
        this->startPlacement(s, m_result.shapes.size(), "Frame colocado (sin triggers): " + std::to_string(m_result.shapes.size()) + " objetos");
        this->onClose(nullptr);
        return;
    }

    auto job = std::make_shared<BuildJob>();
    job->path = m_path;
    job->info = m_info;
    job->fps = m_fps;
    job->chan = m_chan;
    job->lag = m_lag;
    job->lagParams.enable = m_lag;
    job->lagParams.chunkObjects = std::clamp(int(mod->getSettingValue<int>("lag-chunk-objects")), 500, 200000);
    job->lagParams.leadSeconds = std::clamp(mod->getSettingValue<double>("lag-lead"), 0.1, 3.0);
    job->lagParams.farDistance = std::clamp(double(mod->getSettingValue<int>("lag-far")), 1500.0, 300000.0);
    job->lagParams.spread = mod->getSettingValue<bool>("lag-spread");
    job->lagParams.farStep = std::clamp(double(mod->getSettingValue<int>("lag-far-step")), 500.0, 200000.0);
    job->lagParams.farMax = std::clamp(double(mod->getSettingValue<int>("lag-far-max")), 5000.0, 1000000.0);
    job->paletteSize = std::max(8, paletteSize() ? paletteSize() : 128);
    job->method = method();
    job->po = makePlanOptions();
    job->opt = makeOptions(1000);
    job->spec.path = m_path;
    job->spec.start = m_start;
    job->spec.length = m_length;
    job->spec.fps = m_fps;
    job->spec.storeLong = storeLong(N);
    job->spec.ffmpegPath = mod->getSettingValue<std::string>("ffmpeg-path");
    job->spec.allowFfmpeg = mod->getSettingValue<bool>("use-ffmpeg");
    job->opt.maxRaster = std::clamp(int(mod->getSettingValue<int>("max-raster")), 160, 1024);
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    job->threads = int(std::clamp<unsigned>(unsigned(std::max(1, int(mod->getSettingValue<int>("threads")))), 1u, hw));
    m_build = job;
    this->unschedule(schedule_selector(VideoPopup::recompute));
    this->setStatus("Decodificando... 0%", "No cierres la ventana");

    std::thread([job]() {
        try {
            auto sr = vid::sampleVideo(job->spec, job->info, &job->decodeProg, &job->cancelDecode);
            job->backend = sr.backend;
            job->srcW = sr.srcW; job->srcH = sr.srcH;
            if (!sr.ok()) {
                job->error = sr.error.empty() ? "No se pudo decodificar el video" : sr.error;
            } else if (!job->prog.cancel.load()) {
                gdx::VideoClip clip;
                clip.fps = job->fps;
                clip.length = sr.length;                      // duracion real del tramo: el bucle dura EXACTAMENTE lo que el MP4 (a 12 y a 30 fps)
                clip.frames = std::move(sr.frames);
                gdx::stabilizeFrames(clip.frames, job->po.stabilizeTol);
                job->plan = gdx::planVideo(clip, job->po);
                if (job->plan.avgPerGroup < 30.0) {
                    job->error = "No caben tantos frames con el tope de objetos. Acorta la duracion o sube el tope total.";
                } else {
                    job->phase.store(1);
                    job->results = gdx::convertVideo(clip, job->plan, job->method, job->opt, &job->prog, job->threads);
                    if (job->chan && !job->prog.cancel.load()) job->palette = gdx::quantizeVideo(job->results, job->paletteSize);   // paleta global del video
                }
            }
        } catch (std::exception const& e) {
            job->error = std::string("Error interno: ") + e.what();
        } catch (...) {
            job->error = "Error interno (memoria?)";
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void VideoPopup::finishBuild(std::shared_ptr<BuildJob> job) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto* mod = Mod::get();
    if (!job->error.empty()) {
        showAlert("Video Importer", job->error);
        this->applyPreview(nullptr);
        return;
    }

    gdx::VideoExportParams p;
    p.eo = makeExportOptions();
    p.mode = m_mode;
    p.blockSeconds = std::clamp(mod->getSettingValue<double>("spawn-block"), 0.25, 4.0);
    p.timeScale = std::clamp(mod->getSettingValue<double>("time-scale"), 0.5, 2.0);
    p.spreadXBlocks = std::clamp(int(mod->getSettingValue<int>("trigger-spread-x")), 0, 500);
    if (job->chan) p.palette = job->palette;       // vacia = HSV
    p.lag = job->lagParams;
    const CCPoint origin = placementOrigin();
    p.eo.originX = origin.x;
    p.eo.originY = origin.y;
    p.baseGroup = std::max(this->maxUsedGroup() + 1, std::clamp(int(mod->getSettingValue<int>("first-group")), 1, 9000));
    if (mod->getSettingValue<bool>("triggers-at-start")) {
        p.trigX = -30.0;
    } else {
        const gdx::Result* r0 = nullptr;
        for (auto& r : job->results) if (!r.shapes.empty()) { r0 = &r; break; }
        p.trigX = r0 ? origin.x - gdx::gdImageSize(*r0, p.eo).w * 0.5 - 30.0 : -30.0;
    }

    std::string err;
    gdx::LagInfo lagInfo;
    std::string str = gdx::buildVideoString(job->results, job->plan, p, &err, &lagInfo);
    if (str.empty()) {
        showAlert("Video Importer", err.empty() ? std::string("No se pudo generar la animacion.") : err);
        this->applyPreview(nullptr);
        return;
    }
    size_t count = 1;
    for (char c : str) if (c == ';') ++count;
    if (long(count) > long(this->editorRoom())) {          // GD cortaria la cadena y se perderian objetos: mejor no colocar nada
        char em[240];
        std::snprintf(em, sizeof em, "La animacion necesita %zu objetos y en el editor solo caben %d mas (ver \"Limite de objetos del editor\" en Ajustes). Baja el tope total o la duracion.", count, this->editorRoom());
        showAlert("Video Importer", em);
        this->applyPreview(nullptr);
        return;
    }
    const auto S = gdx::scheduleVideo(job->plan, p.mode, nullptr, p.blockSeconds, p.timeScale);
    std::string lagText;
    if (lagInfo.active) lagText = " | " + std::to_string(lagInfo.chunks) + " partes anti-lag";
    else if (lagInfo.requested) lagText = " | anti-lag NO aplicado: " + lagInfo.note;
    char msg[320];
    std::snprintf(msg, sizeof msg, "Video colocado: %.2f s, %d frames (%d distintos), %zu objetos, grupos %d-%d | %s%s%s%s",
                  job->plan.seconds(), job->plan.frames, job->plan.groups(), count, p.baseGroup, p.baseGroup + std::max(0, S.groupsNeeded + (p.mode == gdx::PlayMode::Remap && lagInfo.active ? 3 * lagInfo.chunks : 0) - 1),
                  (p.mode == gdx::PlayMode::Toggle ? "Toggle" : p.mode == gdx::PlayMode::Remap ? "Remap" : "Spawn"), job->chan ? " | canales" : "", lagText.c_str(), job->backend.empty() ? "" : (" | " + job->backend).c_str());
    {   // resumen para el log de "Guardar log al jugar"
        char sum[640];
        std::snprintf(sum, sizeof sum, "bucle %.3f s (escala x%.3f, M se relanza a los %.3f s), %zu objetos, cadena %.1f MB, %d frames (%d distintos), grupos %d-%d, modo %s%s, anti-lag %s (%d partes, espera hasta x+%.0f%s)",
                      job->plan.seconds(), p.timeScale, S.total, count, double(str.size()) / (1024.0 * 1024.0), job->plan.frames, job->plan.groups(), p.baseGroup, p.baseGroup + std::max(0, S.groupsNeeded + (p.mode == gdx::PlayMode::Remap && lagInfo.active ? 3 * lagInfo.chunks : 0) - 1),
                      (p.mode == gdx::PlayMode::Toggle ? "Toggle" : p.mode == gdx::PlayMode::Remap ? "Remap" : "Spawn"), job->chan ? "+canales" : "", lagInfo.active ? "SI" : "no", lagInfo.chunks, lagInfo.farDist, p.lag.spread ? ", cada parte a distinta distancia" : "");
        mod->setSavedValue<std::string>("last-placement", std::string(sum));
    }
    this->startPlacement(str, count, msg);
    this->onClose(nullptr);
}

void VideoPopup::onInfo(CCObject*) {
    MDPopup::create(
        "Video Importer (beta)",
        "Elige un <cy>MP4</c> (H.264 funciona siempre; HEVC/VP9/AV1 dependen de tu sistema). Cada frame se convierte en objetos con el <cy>metodo</c> elegido y va en su propio <cy>grupo</c>.\n\n"
        "- <cg>12 fps / 30 fps</c>: a cuantos fotogramas por segundo se muestrea el video. 12 = mas objetos por frame y videos mas largos; 30 = mas fluido.\n"
        "- <cg>Objetos totales</c>: presupuesto de TODA la animacion (hasta 1000000; por defecto 1000000). Se reparte entre los frames segun su detalle; los frames casi iguales se reutilizan.\n"
        "- <cg>Inicio / Duracion</c>: el tramo del video (hasta 500 s). La duracion maxima sale ademas del tope de objetos y de los 9999 grupos de GD. El bucle dura exactamente lo que el tramo; si en tu GD va mas rapido o lento, usa <cy>Correccion de velocidad</c> en Ajustes.\n\n"
        "<cy>Toggle (exacto)</c>: un Spawn por paso con su instante absoluto; la animacion no se desincroniza nunca.\n"
        "<cy>Spawn (ligero)</c>: los pasos se encadenan y se resincronizan cada segundo; la mitad de Spawn y sin pico al empezar cada vuelta.\n"
        "<cy>Remap (mini)</c>: el metodo mas ligero. Una sola plantilla de 4 Toggle que se reutiliza con <cy>remap</c> en un Spawn por paso (instantes absolutos como Toggle): ~1 trigger y ~1 grupo por frame en vez de ~6 y 2. Caben mas frames y videos mas largos con los 9999 grupos de GD (hasta 500 s a 12 fps). Funciona con las partes anti-lag.\n\n"
        "La animacion usa solo <cy>Toggle</c> y <cy>Spawn</c> triggers. El color va en <cy>HSV</c> dentro de cada objeto; con <cy>Canales</c> los objetos usan canales de color (2..) con una paleta unica para todo el video, fijada por Color triggers (uno por canal) al inicio del nivel: cadena mas corta, pero con menos colores (ver Ajustes). "
        "Los frames ocultos quedan <cg>desactivados</c> (sin lag). Los 2 triggers de arranque van en x=-30 y la animacion se repite <cg>para siempre</c>.\n\n"
        "<cy>Partes anti-lag (Move)</c>: los frames se reparten en partes de ~25000 objetos (Ajustes). Solo una parte esta cerca de la camara; las demas esperan <cg>juntas y lejos</c> en X. Un <cy>Move</c> instantaneo trae la siguiente justo antes de que toque y otro la devuelve lejos al terminar. Menos objetos que GD tenga que recorrer al jugar (la carga del nivel no cambia). Usa Move triggers ademas de Toggle y Spawn.\n\n"
        "Si al jugar el nivel no entra: activa <cy>Guardar log al jugar</c> (Ajustes) y mira <cy>play-log.txt</c>; prueba tambien con ~190000 objetos totales.\n\n"
        "En el editor los frames se ven superpuestos: <cy>dale a jugar</c>. Con 400-500k objetos GD tarda en cargar y necesita un editor sin limite de objetos.\n\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK"
    )->show();
}
