#include "VideoPopup.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/async.hpp>
#include <chrono>
#include <set>
#include <thread>

namespace {
    constexpr float kPopupW = 440.f, kPopupH = 300.f;
    constexpr float kPrevSize = 140.f;
    constexpr int kMinBudget = 20000, kMaxBudget = 500000, kBudgetStep = 5000;
    constexpr int kMinSeconds = 1, kMaxSeconds = 120;

    float budgetToSlider(int v) { return float(v - kMinBudget) / float(kMaxBudget - kMinBudget); }
    int sliderToBudget(float s) { int v = kMinBudget + int(std::lround(s * (kMaxBudget - kMinBudget))); return std::clamp(((v + kBudgetStep / 2) / kBudgetStep) * kBudgetStep, kMinBudget, kMaxBudget); }
    float secondsToSlider(int v) { return float(v - kMinSeconds) / float(kMaxSeconds - kMinSeconds); }
    int sliderToSeconds(float s) { return std::clamp(kMinSeconds + int(std::lround(s * (kMaxSeconds - kMinSeconds))), kMinSeconds, kMaxSeconds); }

    int qualityFromSetting() {
        auto q = Mod::get()->getSettingValue<std::string>("quality");
        if (q == "Normal") return 1;
        if (q == "Alto") return 2;
        if (q == "Maximo") return 3;
        return 0;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Colocacion por tandas (igual que en el GIF Importer): GD corta la cadena si se pasa el limite del editor y congela si se crean decenas
// de miles de objetos de golpe, asi que se crean ~1500 por tanda y unas pocas tandas por fotograma (~25 ms). El nodo cuelga del editor:
// si se sale del editor, la colocacion se cancela sola.
// ---------------------------------------------------------------------------------------------------------------------
namespace {
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
            selectAfter = select && total <= 8000;
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
            if (ok && placed < expected) {
                msg += " | AVISO: GD creo " + std::to_string(placed) + " de " + std::to_string(expected) + " objetos";
                warn = true;
            }
            Notification::create(msg, ok && !warn ? NotificationIcon::Success : NotificationIcon::Error, warn ? 8.f : 4.f)->show();
            this->removeFromParent();
        }
    };

    // Vista previa de UN frame con el metodo elegido (los metodos de mosaico muestran sus teselas).
    vx::Result previewFor(vx::Clip const& clip, vx::Params const& P) {
        const int W = clip.w, H = clip.h;
        const vx::Image& fr = clip.frames[clip.frames.size() / 2];
        vx::Coder c = P.coder;
        const int n = clip.count();
        const int perFrame = std::clamp(P.budget / std::max(1, n), 40, 4000);
        if (c == vx::Coder::Auto) c = perFrame < 90 ? vx::Coder::Mosaic : (perFrame < 420 ? vx::Coder::MosaicDetail : vx::Coder::Reel);
        vx::Result r; r.rasterW = W; r.rasterH = H; r.aspect = float(W) / float(std::max(1, H));
        auto cellsFor = [&](int K) {
            std::vector<vx::Cell> cells = vx::temporalCells({&fr}, K, 2, nullptr);
            auto col = vx::cellMeans(fr, cells);
            for (size_t i = 0; i < cells.size(); ++i) {
                vx::Shape s; s.cx = (cells[i].x0 + cells[i].x1) * 0.5f; s.cy = (cells[i].y0 + cells[i].y1) * 0.5f;
                s.w = float(cells[i].x1 - cells[i].x0); s.h = float(cells[i].y1 - cells[i].y0); s.r = col[i][0]; s.g = col[i][1]; s.b = col[i][2];
                r.shapes.push_back(s);
            }
            return cells;
        };
        if (c == vx::Coder::Mosaic) { cellsFor(P.mosaicMaxCells); r.note = "Vista previa: teselas del frame central"; }
        else if (c == vx::Coder::PulseMosaic) { cellsFor(P.pulseMaxCells); r.note = "Vista previa: teselas (pulsos)"; }
        else if (c == vx::Coder::MosaicDetail) {
            cellsFor(std::min(P.mosaicMaxCells, 560));
            vx::Image canvas = vx::blankCanvas(W, H); vx::paintOnto(canvas, r.shapes);
            auto det = vx::residualShapes(fr, canvas, 15, perFrame, P);
            for (auto& s : det) { s.z = 1; r.shapes.push_back(s); }
            r.note = "Vista previa: mosaico + detalle";
        } else {
            vx::Result f = vx::paintFull(fr, perFrame, P);
            r.shapes = std::move(f.shapes); r.psnr = f.psnr;
            r.note = "Vista previa: frame central (~" + std::to_string(perFrame) + " obj)";
        }
        return r;
    }
}

VideoPopup* VideoPopup::create(EditorUI* editor) {
    auto ret = new VideoPopup();
    if (ret->init(editor)) { ret->autorelease(); return ret; }
    delete ret;
    return nullptr;
}

VideoPopup::~VideoPopup() {
    if (m_load) m_load->prog.cancel.store(true);
    if (m_prev) { /* el hilo de vista previa termina solo */ }
    if (m_build) m_build->prog.cancel.store(true);
}

bool VideoPopup::init(EditorUI* editor) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_editor = editor;
    this->setTitle("Video Importer (beta)");

    auto* mod = Mod::get();
    m_method = std::clamp(mod->getSavedValue<int>("method", int(vx::Coder::Auto)), 0, vx::kCoderCount - 1);
    m_budget = std::clamp(mod->getSavedValue<int>("budget", 300000), kMinBudget, kMaxBudget);
    m_seconds = std::clamp(mod->getSavedValue<int>("seconds", 10), kMinSeconds, kMaxSeconds);
    m_fps30 = mod->getSavedValue<bool>("fps30", true);
    m_gray = mod->getSavedValue<bool>("gray", false);
    m_chan = mod->getSavedValue<bool>("chan", false);

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
    auto hint = CCLabelBMFont::create("Elige un video", "chatFont.fnt");
    hint->setOpacity(120);
    hint->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(hint);
    put(m_previewHolder, 84.f, 112.f);

    m_statA = label("MP4, MOV, MKV, GIF o carpeta", "chatFont.fnt", 0.8f, 84.f, 194.f);
    m_statB = label("", "chatFont.fnt", 0.75f, 84.f, 207.f);
    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt", 150.f, kCCTextAlignmentCenter);
    m_noteLabel->setScale(0.6f);
    m_noteLabel->setColor({255, 200, 80});
    put(m_noteLabel, 84.f, 238.f);

    // ============ derecha ============
    auto arrow = [&](bool flip, SEL_MenuHandler cb, float x, float y) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
        spr->setScale(0.5f);
        spr->setFlipX(flip);
        putBtn(CCMenuItemSpriteExtra::create(spr, this, cb), x, y);
    };
    arrow(false, menu_selector(VideoPopup::onPrev), 178.f, 52.f);
    arrow(true,  menu_selector(VideoPopup::onNext), 416.f, 52.f);
    m_methodLabel = label("", "bigFont.fnt", 0.5f, 297.f, 52.f);
    m_descLabel = CCLabelBMFont::create("", "chatFont.fnt", 390.f, kCCTextAlignmentCenter);
    m_descLabel->setScale(0.62f);
    put(m_descLabel, 297.f, 84.f);

    label("Presupuesto de objetos", "bigFont.fnt", 0.32f, 170.f, 122.f, 0.f);
    m_budgetValue = label("", "bigFont.fnt", 0.32f, 424.f, 122.f, 1.f);
    m_budgetSlider = Slider::create(this, menu_selector(VideoPopup::onBudgetSlider), 0.78f);
    m_budgetSlider->setValue(budgetToSlider(m_budget));
    put(m_budgetSlider, 297.f, 138.f);

    label("Duracion a convertir", "bigFont.fnt", 0.32f, 170.f, 160.f, 0.f);
    m_secondsValue = label("", "bigFont.fnt", 0.32f, 424.f, 160.f, 1.f);
    m_secondsSlider = Slider::create(this, menu_selector(VideoPopup::onSecondsSlider), 0.78f);
    m_secondsSlider->setValue(secondsToSlider(m_seconds));
    put(m_secondsSlider, 297.f, 176.f);

    // dos checks de fps (excluyentes): el video se procesa a la tasa marcada
    m_chk12 = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VideoPopup::on12), 0.5f);
    m_chk12->toggle(!m_fps30);
    putBtn(m_chk12, 180.f, 202.f);
    label("12 fps (barato)", "bigFont.fnt", 0.28f, 192.f, 202.f, 0.f);
    m_chk30 = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VideoPopup::on30), 0.5f);
    m_chk30->toggle(m_fps30);
    putBtn(m_chk30, 312.f, 202.f);
    label("30 fps (fluido)", "bigFont.fnt", 0.28f, 324.f, 202.f, 0.f);

    m_chkGray = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VideoPopup::onGray), 0.5f);
    m_chkGray->toggle(m_gray);
    putBtn(m_chkGray, 180.f, 226.f);
    label("Escala de grises", "bigFont.fnt", 0.28f, 192.f, 226.f, 0.f);
    m_chkChan = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(VideoPopup::onChan), 0.5f);
    m_chkChan->toggle(m_chan);
    putBtn(m_chkChan, 312.f, 226.f);
    if (auto* l = label("Canales (sin HSV)", "bigFont.fnt", 0.28f, 324.f, 226.f, 0.f)) l->limitLabelWidth(106.f, 0.28f, 0.15f);

    auto pickBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Video", "bigFont.fnt", "GJ_button_01.png", 0.6f), this, menu_selector(VideoPopup::onPick));
    putBtn(pickBtn, 212.f, 268.f);
    auto dirBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Carpeta", "bigFont.fnt", "GJ_button_04.png", 0.5f), this, menu_selector(VideoPopup::onPickFolder));
    putBtn(dirBtn, 280.f, 268.f);
    auto placeBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Colocar", "bigFont.fnt", "GJ_button_02.png", 0.6f), this, menu_selector(VideoPopup::onPlace));
    putBtn(placeBtn, 358.f, 268.f);
    auto infoBtn = CCMenuItemSpriteExtra::create(CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"), this, menu_selector(VideoPopup::onInfo));
    putBtn(infoBtn, 418.f, 268.f);
    auto setBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Ajustes", "bigFont.fnt", "GJ_button_04.png", 0.4f), this, menu_selector(VideoPopup::onSettings));
    putBtn(setBtn, 40.f, 270.f);
    auto testBtn = CCMenuItemSpriteExtra::create(ButtonSprite::create("Prueba", "bigFont.fnt", "GJ_button_04.png", 0.4f), this, menu_selector(VideoPopup::onTest));
    putBtn(testBtn, 112.f, 270.f);

    this->schedule(schedule_selector(VideoPopup::tick), 0.1f);
    this->refreshLabels();
    return true;
}

// ------------------------------------------------------------- etiquetas ---

void VideoPopup::refreshLabels() {
    auto const& info = vx::coderInfo(vx::Coder(m_method));
    m_methodLabel->setString(info.name);
    m_methodLabel->limitLabelWidth(220.f, 0.5f, 0.2f);
    std::string d = info.desc;
    d += "\n[" + std::string(info.triggers) + "]";
    m_descLabel->setString(d.c_str());

    char buf[96];
    if (m_clip && m_clip->count() > 0) std::snprintf(buf, sizeof buf, "%d (~%d/frame)", m_budget, m_budget / m_clip->count());
    else std::snprintf(buf, sizeof buf, "%d", m_budget);
    m_budgetValue->setString(buf);
    if (m_clip && m_clip->srcDuration > 0.5 && double(m_seconds) > m_clip->srcDuration + 0.5) std::snprintf(buf, sizeof buf, "%d s (el video dura %.0f)", m_seconds, m_clip->srcDuration);
    else std::snprintf(buf, sizeof buf, "%d s", m_seconds);
    m_secondsValue->setString(buf);

    auto* mod = Mod::get();
    mod->setSavedValue<int>("method", m_method);
    mod->setSavedValue<int>("budget", m_budget);
    mod->setSavedValue<int>("seconds", m_seconds);
    mod->setSavedValue<bool>("fps30", m_fps30);
    mod->setSavedValue<bool>("gray", m_gray);
    mod->setSavedValue<bool>("chan", m_chan);
}

void VideoPopup::setStatus(std::string const& a, std::string const& b, std::string const& note) {
    m_statA->setString(a.c_str());
    m_statA->limitLabelWidth(150.f, 0.8f, 0.35f);
    m_statB->setString(b.c_str());
    m_statB->limitLabelWidth(150.f, 0.75f, 0.35f);
    m_noteLabel->setString(note.c_str());
}

void VideoPopup::stepMethod(int dir) {
    m_method = (m_method + dir + vx::kCoderCount) % vx::kCoderCount;
    this->refreshLabels();
    this->schedulePreview();
}

void VideoPopup::onBudgetSlider(CCObject* sender) {
    int v = sliderToBudget(static_cast<SliderThumb*>(sender)->getValue());
    if (v == m_budget) return;
    m_budget = v;
    this->refreshLabels();
    this->schedulePreview();
}

void VideoPopup::onSecondsSlider(CCObject* sender) {
    int v = sliderToSeconds(static_cast<SliderThumb*>(sender)->getValue());
    if (v == m_seconds) return;
    m_seconds = v;
    this->refreshLabels();
    this->scheduleReload();                     // hay que volver a leer el video con la nueva duracion
}

void VideoPopup::syncFpsChecks(float) {
    m_chk12->toggle(!m_fps30);
    m_chk30->toggle(m_fps30);
}
void VideoPopup::on12(CCObject*) {
    m_fps30 = false;                            // los dos checks son excluyentes; el estado visual se corrige justo despues del callback
    this->scheduleOnce(schedule_selector(VideoPopup::syncFpsChecks), 0.f);
    this->refreshLabels();
    this->scheduleReload();
}
void VideoPopup::on30(CCObject*) {
    m_fps30 = true;
    this->scheduleOnce(schedule_selector(VideoPopup::syncFpsChecks), 0.f);
    this->refreshLabels();
    this->scheduleReload();
}
void VideoPopup::onGray(CCObject*) {
    m_gray = !m_gray;                           // en el callback el toggler aun no ha cambiado su estado
    this->refreshLabels();
    this->scheduleReload();
}
void VideoPopup::onChan(CCObject*) {
    m_chan = !m_chan;
    this->refreshLabels();
}

void VideoPopup::onSettings(CCObject*) { openSettingsPopup(Mod::get()); }

// ------------------------------------------------------------ cargar video --

void VideoPopup::onPick(CCObject*) {
    if (m_build) return;
    auto options = file::FilePickOptions{
        std::nullopt,
        {{"Video o GIF", {"*.mp4", "*.mov", "*.mkv", "*.webm", "*.avi", "*.wmv", "*.m4v", "*.gif"}}}
    };
    async::spawn(
        file::pick(file::PickMode::OpenFile, options),
        [weak = WeakRef<VideoPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) { self->setStatus("No se pudo abrir", "el selector de archivos"); return; }
            auto opt = result.unwrap();
            if (!opt) return;                                    // cancelado
            self->load(opt.value());
        }
    );
}

void VideoPopup::onPickFolder(CCObject*) {
    if (m_build) return;
    async::spawn(
        file::pick(file::PickMode::OpenFolder, file::FilePickOptions{}),
        [weak = WeakRef<VideoPopup>(this)](auto result) {
            auto self = weak.lock();
            if (!self) return;
            if (!result.isOk()) { self->setStatus("No se pudo abrir", "el selector de carpetas"); return; }
            auto opt = result.unwrap();
            if (!opt) return;
            self->load(opt.value());
        }
    );
}

void VideoPopup::load(std::filesystem::path const& path) {
    m_path = path;
    this->reload();
}

void VideoPopup::scheduleReload() {
    if (m_path.empty() || m_build) return;
    this->unschedule(schedule_selector(VideoPopup::reload));
    this->scheduleOnce(schedule_selector(VideoPopup::reload), 0.6f);
}

void VideoPopup::reload(float) {
    if (m_path.empty() || m_build) return;
    if (m_load) m_load->prog.cancel.store(true);
    auto* mod = Mod::get();
    auto job = std::make_shared<LoadJob>();
    job->id = ++m_jobCounter;
    job->req.path = m_path;
    job->req.fps = fps();
    job->req.startSec = mod->getSettingValue<double>("start-sec");
    job->req.maxSeconds = m_seconds;
    job->req.maxSide = int(mod->getSettingValue<int>("max-raster"));
    job->req.seqFps = mod->getSettingValue<double>("sequence-fps");
    job->req.gray = m_gray;
    job->req.ffmpegPath = mod->getSettingValue<std::string>("ffmpeg-path");
    job->req.prog = &job->prog;
    m_load = job;
    m_hasResult = false;
    this->setStatus("Leyendo el video...", "", "");
    std::thread([job]() {
        try {
            std::string err;
            if (vx::decodeVideo(job->req, job->clip, err)) job->shots = int(vx::detectShots(job->clip).size());
            else job->error = err;
        } catch (std::exception const& e) {
            job->error = std::string("Error al leer el video: ") + e.what();
        } catch (...) {
            job->error = "Error interno al leer el video";
        }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void VideoPopup::schedulePreview() {
    if (!m_clip || m_build) return;
    this->unschedule(schedule_selector(VideoPopup::recomputePreview));
    this->scheduleOnce(schedule_selector(VideoPopup::recomputePreview), 0.35f);
}

void VideoPopup::recomputePreview(float) {
    if (!m_clip || m_build) return;
    auto job = std::make_shared<PreviewJob>();
    job->id = ++m_jobCounter;
    job->clip = m_clip;
    job->params = makeParams(int(std::lround(m_clip->fps)));
    m_prev = job;
    std::thread([job]() {
        try { job->result = previewFor(*job->clip, job->params); }
        catch (...) { job->result = vx::Result{}; job->result.note = "Error interno en la vista previa"; }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void VideoPopup::tick(float) {
    // --- lectura del video
    if (m_load && m_load->done.load(std::memory_order_acquire)) {
        auto job = m_load; m_load = nullptr;
        {
            if (!job->error.empty()) {
                m_clip = nullptr;
                this->setStatus("No se pudo leer", "", job->error);
                FLAlertLayer::create("Video Importer", job->error.c_str(), "OK")->show();
            } else {
                m_clip = std::make_shared<vx::Clip>(std::move(job->clip));
                m_shots = job->shots;
                char a[96], b[96];
                std::snprintf(a, sizeof a, "%d frames | %dx%d", m_clip->count(), m_clip->w, m_clip->h);
                std::snprintf(b, sizeof b, "%.1f s | %d escenas", m_clip->seconds(), m_shots);
                this->setStatus(a, b, "");
                this->refreshLabels();
                this->recomputePreview();
            }
        }
    } else if (m_load) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "Leyendo el video... %d%%", int(m_load->prog.fraction.load() * 100.f));
        this->setStatus(buf, "");
    }
    // --- codificacion completa
    if (m_build) {
        if (m_build->done.load(std::memory_order_acquire)) {
            auto job = m_build; m_build = nullptr;
            if (!job->prog.cancel.load()) this->finishBuild(job);
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "Convirtiendo... %d%%", int(m_build->prog.fraction.load() * 100.f));
        this->setStatus(buf, "No cierres la ventana");
        return;
    }
    // --- vista previa
    if (m_prev && m_prev->done.load(std::memory_order_acquire)) {
        auto job = m_prev; m_prev = nullptr;
        if (job->id <= m_jobCounter) this->applyPreview(job);
    }
}

void VideoPopup::applyPreview(std::shared_ptr<PreviewJob> job) {
    if (job) { m_result = std::move(job->result); m_hasResult = !m_result.shapes.empty(); }
    if (!m_hasResult) { this->setStatus("No se genero vista previa", "", m_result.note); return; }
    this->buildPreviewSprite();
    if (m_clip) {
        char a[96], b[96];
        std::snprintf(a, sizeof a, "%d frames | %dx%d", m_clip->count(), m_clip->w, m_clip->h);
        std::snprintf(b, sizeof b, "%.1f s | %d escenas", m_clip->seconds(), m_shots);
        this->setStatus(a, b, m_result.note);
    }
}

void VideoPopup::buildPreviewSprite() {
    if (m_preview) { m_preview->removeFromParent(); m_preview = nullptr; }
    int w = m_result.rasterW, h = m_result.rasterH;
    if (w <= 0 || h <= 0) return;
    vx::Image img = gdx::renderShapesScaled(m_result.shapes, w, h, 1);
    w = img.w; h = img.h;
    std::vector<uint8_t> buf(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        const uint8_t* p = &img.rgba[i * 4];
        float a = p[3] / 255.f;
        for (int c = 0; c < 3; ++c) buf[i * 4 + c] = uint8_t(p[c] * a + 40 * (1.f - a));
        buf[i * 4 + 3] = 255;
    }
    auto tex = new CCTexture2D();
    if (!tex->initWithData(buf.data(), kCCTexture2DPixelFormat_RGBA8888, w, h, CCSize(float(w), float(h)))) { delete tex; return; }
    tex->autorelease();
    m_preview = CCSprite::createWithTexture(tex);
    if (!m_preview) return;
    float aspect = m_result.aspect > 0 ? m_result.aspect : 1.f;
    float tw = aspect >= 1.f ? kPrevSize : kPrevSize * aspect;
    float th = aspect >= 1.f ? kPrevSize / aspect : kPrevSize;
    m_preview->setScaleX(tw / w);
    m_preview->setScaleY(th / h);
    m_preview->setPosition({kPrevSize / 2, kPrevSize / 2});
    m_previewHolder->addChild(m_preview);
}

// ---------------------------------------------------------------- nivel -----

int VideoPopup::editorLimit() const {
    return std::clamp(int(Mod::get()->getSettingValue<int>("editor-limit")), 50000, 2000000);
}
int VideoPopup::editorRoom() const {
    auto lel = LevelEditorLayer::get();
    int cur = (lel && lel->m_objects) ? int(lel->m_objects->count()) : 0;
    return std::max(0, editorLimit() - 3000 - cur);
}

CCPoint VideoPopup::viewCenter() const {
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

// canales de color 2..999 que el nivel todavia no usa (los mosaicos los reutilizan escena a escena)
std::vector<int> VideoPopup::freeChannels() const {
    std::set<int> used;
    auto lel = LevelEditorLayer::get();
    if (lel && lel->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(lel->m_objects)) {
            if (!obj) continue;
            if constexpr (requires { obj->m_baseColor->m_colorID; }) { if (obj->m_baseColor) used.insert(int(obj->m_baseColor->m_colorID)); }
            if constexpr (requires { obj->m_detailColor->m_colorID; }) { if (obj->m_detailColor) used.insert(int(obj->m_detailColor->m_colorID)); }
            if (auto* eff = typeinfo_cast<EffectGameObject*>(obj)) {
                if constexpr (requires { eff->m_targetColorID; }) used.insert(int(eff->m_targetColorID));
            }
        }
    }
    std::vector<int> v;
    for (int c = 2; c <= vx::kChannelMax; ++c) if (!used.count(c)) v.push_back(c);
    return v;
}

vx::Params VideoPopup::makeParams(int fpsV) const {
    auto* mod = Mod::get();
    vx::Params P;
    P.coder = vx::Coder(m_method);
    P.fps = fpsV;
    P.budget = std::min(m_budget, std::max(2000, this->editorRoom()));
    P.maxRaster = int(mod->getSettingValue<int>("max-raster"));
    P.quality = qualityFromSetting();
    P.gray = m_gray;
    P.channels = m_chan;
    P.paletteK = int(mod->getSettingValue<int>("palette-colors"));
    P.smooth = mod->getSettingValue<bool>("smooth-colors");
    P.reassert = mod->getSettingValue<bool>("reassert");
    P.kfStep = int(mod->getSettingValue<int>("crossfade-step"));
    P.mosaicMaxCells = int(mod->getSettingValue<int>("mosaic-max-cells"));
    P.pulseMaxCells = int(mod->getSettingValue<int>("pulse-max-cells"));
    int fg = int(mod->getSettingValue<int>("first-group"));
    P.firstGroup = fg > 0 ? fg : this->maxUsedGroup() + 1;
    P.freeChannels = this->freeChannels();
    P.colorChannel = int(mod->getSettingValue<int>("color-channel"));
    P.bleed = mod->getSettingValue<double>("bleed");
    P.alphaThr = 128;
    P.flipRotation = mod->getSettingValue<bool>("flip-rotation");
    P.threads = int(std::clamp<unsigned>(unsigned(std::max(1, int(mod->getSettingValue<int>("threads")))), 1u, std::max(1u, std::thread::hardware_concurrency())));
    // zona de la cinta: por defecto ARRIBA del nivel (el jugador nunca la toca); o justo en la vista del editor
    CCPoint vc = this->viewCenter();
    double fw = 569, fh = 320;
    if (m_clip) vx::frameBox(m_clip->w, m_clip->h, fw, fh);
    P.originX = vc.x - (fw / 2 + 420.0);
    if (mod->getSettingValue<std::string>("origin-mode") == "En la vista del editor") P.originY = vc.y - (fh / 2 + 260.0);
    else P.originY = double(mod->getSettingValue<int>("origin-y"));
    P.startX = mod->getSettingValue<bool>("triggers-at-start") ? -30.0 : P.originX - 100.0;
    return P;
}

void VideoPopup::startPlacement(std::vector<std::string> chunks, size_t objectCount, std::string const& doneMsg) {
    auto lel = LevelEditorLayer::get();
    if (!lel) return;
    auto runner = PlaceRunner::create();
    if (!runner) return;
    lel->addChild(runner);
    runner->begin(std::move(chunks), objectCount, Mod::get()->getSettingValue<bool>("select-after"), doneMsg);
}

// --------------------------------------------------------------- colocar ----

void VideoPopup::onPlace(CCObject*) {
    if (m_build) return;
    if (m_load) { FLAlertLayer::create("Video Importer", "Todavia se esta leyendo el video. Espera un momento.", "OK")->show(); return; }
    if (!m_clip) { FLAlertLayer::create("Video Importer", "Primero elige un video, un GIF o una carpeta de imagenes.", "OK")->show(); return; }
    if (!LevelEditorLayer::get()) return;
    if (this->editorRoom() < 2000) {
        FLAlertLayer::create("Video Importer", "El nivel ya esta casi en el limite de objetos del editor (ajuste \"Limite de objetos del editor\"). No hay sitio para el video.", "OK")->show();
        return;
    }
    auto job = std::make_shared<BuildJob>();
    job->clip = m_clip;
    job->fps = int(std::lround(m_clip->fps));
    job->params = this->makeParams(job->fps);
    m_build = job;
    std::thread([job]() {
        try { job->out = vx::encode(*job->clip, job->params, &job->prog); }
        catch (std::exception const& e) { job->out.error = std::string("Error interno: ") + e.what(); }
        catch (...) { job->out.error = "Error interno al convertir"; }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

// Patron de prueba: 7 escenas sinteticas de 1 s (una por metodo) para comprobar en el juego que los triggers funcionan.
void VideoPopup::onTest(CCObject*) {
    if (m_build) return;
    if (!LevelEditorLayer::get()) return;
    auto job = std::make_shared<BuildJob>();
    job->test = true;
    job->fps = fps();
    job->params = this->makeParams(job->fps);
    m_build = job;
    std::thread([job]() {
        try { job->out = vx::encodeTestPattern(job->params, job->fps); }
        catch (...) { job->out.error = "Error interno en el patron de prueba"; }
        job->done.store(true, std::memory_order_release);
    }).detach();
}

void VideoPopup::finishBuild(std::shared_ptr<BuildJob> job) {
    auto const& o = job->out;
    if (!o.ok()) {
        std::string e = o.error.empty() ? "No se genero nada" : o.error;
        this->setStatus("No se pudo convertir", "", e);
        FLAlertLayer::create("Video Importer", e.c_str(), "OK")->show();
        return;
    }
    if (int(o.objects()) > this->editorRoom()) {
        std::string e = "La animacion tiene " + std::to_string(o.objects()) + " objetos y en el editor solo caben " + std::to_string(this->editorRoom()) +
                        ". Baja el presupuesto o sube el \"Limite de objetos del editor\" en Ajustes (solo si tu GD lo permite).";
        FLAlertLayer::create("Video Importer", e.c_str(), "OK")->show();
        return;
    }
    char a[128], b[128];
    std::snprintf(a, sizeof a, "%zu objetos | %zu escenas", o.objects(), o.shots.size());
    std::snprintf(b, sizeof b, "%.1f dB | %.1f s | %d grupos", o.psnr, o.seconds, o.groupsUsed);
    this->setStatus(a, b, o.note);
    std::string msg = std::string(job->test ? "Patron de prueba" : "Video") + " colocado: " + std::to_string(o.objects()) + " objetos, " +
                      std::to_string(o.shots.size()) + " escenas, " + std::to_string(int(o.psnr)) + " dB. Dale a jugar";
    this->startPlacement(job->out.chunks, o.objects(), msg);
    if (job->test) {
        FLAlertLayer::create("Patron de prueba",
            "Dale a jugar: debes ver <cy>7 escenas de 1 s</c> y el bucle. En cada una se ve un cuadrado blanco que avanza:\n"
            "1 cinta (rojo) | 2 mosaico (verde, se oscurece suave) | 3 mosaico+detalle (azul) | 4 epocas (amarillo) | "
            "5 panoramica (morado, textura que se desplaza) | 6 fundido (cian) | 7 pulsos (naranja).\n"
            "Si una escena sale mal, ese trigger no se comporta como espera el mod en tu version de GD: usa los otros metodos.",
            "OK")->show();
    }
}

void VideoPopup::onInfo(CCObject*) {
    FLAlertLayer::create(
        "Video Importer (beta)",
        "El video se parte en <cy>escenas</c>. Cada escena vive en su propio cuadro del editor (zona alta del nivel) y una "
        "<cy>Static Camera</c> salta de una a otra: GD solo dibuja lo que esta en camara, asi no hay lag aunque haya 400.000 objetos.\n"
        "- <cg>12 / 30 fps</c>: tasa a la que se muestrea el video (marca uno). Mas fps = mas fluido pero mas objetos.\n"
        "- <cg>Presupuesto</c>: objetos totales (formas + triggers). Nunca se supera.\n"
        "- <cg>Metodo 8 (Director)</c> elige el mejor metodo para cada escena.\n"
        "- <cg>Prueba</c>: coloca 7 escenas de 1 s para verificar los triggers en tu GD.\n"
        "- En el editor todo se ve apilado/en calma: <cy>dale a jugar</c> para ver el video (la camara salta sola).\n"
        "- MP4/MOV: Windows lo lee nativo; macOS/Linux necesitan <cy>ffmpeg</c>. En Android/iOS usa GIF o una carpeta de imagenes.\n"
        "No se puede deshacer: <cr>guarda antes</c>.",
        "OK"
    )->show();
}
