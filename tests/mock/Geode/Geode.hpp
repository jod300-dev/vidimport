// Mock MINIMO de la API de Geode/Cocos: solo para COMPILAR VideoPopup.cpp en un PC normal y cazar errores de tipos/nombres.
// No simula nada: las firmas son las que usa el mod. (La API real puede diferir en detalles: la comprobacion definitiva es el CI de GitHub.)
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
struct CCObject { virtual ~CCObject() = default; void release() {} void retain() {} };
struct CCPoint { float x = 0, y = 0; };
struct CCSize { float width = 0, height = 0; CCSize(float w = 0, float h = 0) : width(w), height(h) {} };
inline CCPoint ccp(float x, float y) { return {x, y}; }
struct ccColor3B { uint8_t r, g, b; };
struct ccColor4B { uint8_t r, g, b, a; };
inline ccColor4B ccc4(int r, int g, int b, int a) { return {uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)}; }
using SEL_MenuHandler = void (CCObject::*)(CCObject*);
using SEL_SCHEDULE = void (CCObject::*)(float);
#define menu_selector(f) (SEL_MenuHandler)(&f)
#define schedule_selector(f) (SEL_SCHEDULE)(&f)
enum class Anchor { TopLeft, Bottom };
enum CCTextAlignment { kCCTextAlignmentCenter };
enum CCTexture2DPixelFormat { kCCTexture2DPixelFormat_RGBA8888 };
struct CCNode : CCObject {
    static CCNode* create() { return new CCNode(); }
    void setContentSize(CCSize) {} CCSize getContentSize() const { return {10, 10}; }
    void setAnchorPoint(CCPoint) {} void setPosition(CCPoint) {} void setScale(float) {} void setScaleX(float) {} void setScaleY(float) {}
    void setOpacity(int) {} void setColor(ccColor3B) {}
    void addChild(CCNode*, int = 0) {} void removeFromParent() {} CCNode* getParent() { return nullptr; }
    void addChildAtPosition(CCNode*, Anchor, CCPoint) {}
    CCPoint convertToNodeSpace(CCPoint p) { return p; }
    void schedule(SEL_SCHEDULE, float = 0) {} void unschedule(SEL_SCHEDULE) {} void scheduleOnce(SEL_SCHEDULE, float) {}
    bool init() { return true; } void autorelease() {} void release() {} void retain() {}
};
struct CCLayerColor : CCNode { static CCLayerColor* create(ccColor4B, float, float) { return new CCLayerColor(); } };
struct CCLabelBMFont : CCNode {
    static CCLabelBMFont* create(const char*, const char*, float = 0, CCTextAlignment = kCCTextAlignmentCenter) { return new CCLabelBMFont(); }
    void setString(const char*) {} void limitLabelWidth(float, float, float) {} void setWidth(float) {}
};
struct CCSprite : CCNode {
    static CCSprite* createWithSpriteFrameName(const char*) { return new CCSprite(); }
    static CCSprite* createWithTexture(struct CCTexture2D*) { return new CCSprite(); }
    void setFlipX(bool) {}
};
struct CCTexture2D : CCObject { bool initWithData(const void*, CCTexture2DPixelFormat, int, int, CCSize) { return true; } void autorelease() {} };
struct CCMenuItem : CCNode {};
struct CCMenuItemSpriteExtra : CCMenuItem { static CCMenuItemSpriteExtra* create(CCNode*, CCObject*, SEL_MenuHandler) { return new CCMenuItemSpriteExtra(); } };
struct CCMenuItemToggler : CCMenuItem {
    static CCMenuItemToggler* createWithStandardSprites(CCObject*, SEL_MenuHandler, float) { return new CCMenuItemToggler(); }
    void toggle(bool) {}
};
struct ButtonSprite : CCNode { static ButtonSprite* create(const char*, const char*, const char*, float) { return new ButtonSprite(); } };
struct SliderThumb : CCNode { float getValue() { return 0; } };
struct Slider : CCNode { static Slider* create(CCObject*, SEL_MenuHandler, float) { return new Slider(); } void setValue(float) {} };
struct CCArray : CCObject {
    static CCArray* create() { return new CCArray(); } unsigned count() { return 0; } CCObject* objectAtIndex(unsigned) { return nullptr; }
    void addObject(CCObject*) {}
};
template <class T> struct CCArrayExt { CCArrayExt(CCArray*) {} T* begin() { return nullptr; } T* end() { return nullptr; } };
struct GameObject : CCNode { float getPositionX() { return 0; } float getPositionY() { return 0; } int m_groupCount = 0; int m_objectID = 0; };
struct EffectGameObject : GameObject { int m_targetGroupID = 0, m_centerGroupID = 0; };
template <class T, class U> T typeinfo_cast(U* u) { return dynamic_cast<T>(u); }
struct EditorUI : CCNode { static EditorUI* get() { return nullptr; } CCArray* getSelectedObjects() { return nullptr; } void selectObjects(CCArray*, bool) {} };
struct LevelEditorLayer : CCNode {
    static LevelEditorLayer* get() { return nullptr; }
    CCArray* m_objects = nullptr; CCNode* m_objectLayer = nullptr;
    CCArray* createObjectsFromString(const char*, bool, bool) { return nullptr; }
};
struct CCDirector { static CCDirector* sharedDirector() { static CCDirector d; return &d; } static CCDirector* get() { return sharedDirector(); }
    CCNode* getRunningScene() { return nullptr; } CCSize getWinSize() { return {}; } };
inline CCPoint ccpConvert() { return {}; }
namespace geode {
struct Popup : CCNode {
    CCNode* m_mainLayer = new CCNode(); CCNode* m_buttonMenu = new CCNode();
    bool init(float, float) { return true; } void setTitle(const char*) {} void show() {} void onClose(CCObject*) {}
};
template <class T> struct WeakRef { WeakRef(T* t) : p(t) {} T* lock() const { return p; } T* p; };
template <class T> struct Ref { T* p = nullptr; Ref& operator=(T* t) { p = t; return *this; } T* operator->() const { return p; } operator T*() const { return p; } explicit operator bool() const { return p; } };
enum class NotificationIcon { Success, Error, Info };
struct Notification { static Notification* create(std::string const&, NotificationIcon, float) { return new Notification(); } void show() {} };
struct FLAlertLayer { static FLAlertLayer* create(const char*, const char*, const char*) { return new FLAlertLayer(); } void show() {} };
struct MDPopup { static MDPopup* create(const char*, std::string const&, const char*) { return new MDPopup(); } void show() {} };
namespace file {
enum class PickMode { OpenFile };
struct FilePickOptions { std::optional<std::filesystem::path> defaultPath; std::vector<std::pair<std::string, std::vector<std::string>>> filters; };
template <class T> struct R { bool isOk() const { return true; } T unwrap() const { return T{}; } };
inline int pick(PickMode, FilePickOptions) { return 0; }
}
namespace async { template <class F, class G> void spawn(F, G) {} }
namespace utils::string { inline std::string pathToString(std::filesystem::path const& p) { return p.string(); } }
struct Mod {
    static Mod* get() { static Mod m; return &m; }
    template <class T> T getSettingValue(const char*) { return T{}; }
    template <class T> T getSavedValue(const char*, T d) { return d; }
    template <class T> void setSavedValue(const char*, T) {}
    std::filesystem::path getSaveDir() { return "/tmp/x/y"; }
};
inline void openSettingsPopup(Mod*) {}
namespace prelude { using namespace geode; }
}

// --- clases de GD para los hooks de playlog.cpp (solo las firmas que usa el mod)
namespace gd { using string = std::string; }
struct GJGameLevel : CCObject { gd::string m_levelName; gd::string m_levelString; };
struct PlayLayer : CCNode { bool init(GJGameLevel*, bool, bool) { return true; } void setupHasCompleted() {} void update(float) {} void onQuit() {} void levelComplete() {} };
struct LevelInfoLayer : CCNode { GJGameLevel* m_level = nullptr; bool init(GJGameLevel*, bool) { return true; } void onPlay(CCObject*) {} };
struct EditorPauseLayer : CCNode { void onSaveAndPlay(CCObject*) {} };
#define $modify(cls, base) cls : public base
