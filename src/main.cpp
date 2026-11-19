#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "ui/VideoPopup.hpp"

using namespace geode::prelude;

// Boton en la barra "Edit" del editor que abre el importador de video (mismo enganche que Image Exporter / GIF Importer).
class $modify(VideoImportEditorUI, EditorUI) {
    void onOpenVideoImporter(CCObject*) {
        VideoPopup::create(this)->show();
    }

    void createMoveMenu() {
        EditorUI::createMoveMenu();

        CCMenuItemSpriteExtra* btn = this->getSpriteButton(
            "gdVideoBtn.png"_spr,
            menu_selector(VideoImportEditorUI::onOpenVideoImporter),
            nullptr, 0.9f
        );
        btn->setID("video-import-button"_spr);
        m_editButtonBar->m_buttonArray->addObject(btn);

        int rows = GameManager::sharedState()->getIntGameVariable("0049");
        int cols = GameManager::sharedState()->getIntGameVariable("0050");
        m_editButtonBar->reloadItems(rows, cols);
    }
};
