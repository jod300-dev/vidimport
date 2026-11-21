// ============================================================================
//  Geometria de la ventana del importador de video (sin dependencias de Geode: la usan VideoPopup::init y tests/layout_test.cpp).
//  Unidades de la ventana: X desde la izquierda, Y desde ARRIBA (igual que put(x, y) en VideoPopup::init).
//  La pantalla de GD mide 320 de alto: la ventana no pasa de 290.
//
//      +--------------------------------------------------------------+  <- 0
//      | (X)            Video Importer (beta)                    (i)  |
//      |  +--------+   <  nombre del metodo  >                        |  y 48
//      |  |        |      descripcion (248 x 28)                      |  y 58..86
//      |  | vista  |   Objetos totales ................. 450000       |  y 98   / slider 110
//      |  | previa |   Lado largo ...................... 600 u        |  y 126  / slider 138
//      |  +--------+   Inicio .......................... 0.0 s       |  y 154  / slider 166
//      |  estado       Duracion ......................... 12.0 s      |  y 182  / slider 194
//      |  nota         [x] 12 fps  [ ] 30 fps   [ ] Grises            |  y 214
//      |               [x] Toggle  [ ] Spawn                          |  y 231
//      |  [Ajustes][UI]                       [ Video ] [ Colocar ]   |  y 262
//      +--------------------------------------------------------------+  <- 290
// ============================================================================
#pragma once

namespace gdx::layout {

constexpr float kW = 430.f, kH = 290.f;
constexpr float kMargin = 14.f;

// columna izquierda
constexpr float kPrevSize = 140.f;
constexpr float kLeftL = kMargin, kLeftR = kMargin + kPrevSize;   // 14 .. 154
constexpr float kLeftCx = (kLeftL + kLeftR) / 2.f;                // 84
constexpr float kPrevCy = 112.f;                                  // y = 42 .. 182
constexpr float kStatACy = 194.f, kStatBCy = 209.f;
constexpr float kNoteCy = 229.f, kNoteMaxW = kPrevSize + 10.f, kNoteMaxH = 22.f;

// columna derecha
constexpr float kRightL = 168.f, kRightR = kW - kMargin;          // 168 .. 416
constexpr float kRightCx = (kRightL + kRightR) / 2.f;             // 292
constexpr float kArrowW = 16.5f;
constexpr float kArrowCy = 48.f;
constexpr float kArrowCxL = kRightL + kArrowW / 2.f, kArrowCxR = kRightR - kArrowW / 2.f;
constexpr float kTitleGap = 6.f;
constexpr float kTitleMaxW = (kArrowCxR - kArrowCxL) - kArrowW - 2.f * kTitleGap;
constexpr float kDescTop = 58.f, kDescBottom = 86.f;
constexpr float kDescCy = (kDescTop + kDescBottom) / 2.f;
constexpr float kDescMaxW = kRightR - kRightL, kDescMaxH = kDescBottom - kDescTop;            // 248 x 28

// 4 deslizadores: etiqueta y valor en la misma fila; el deslizador 12 mas abajo
constexpr float kSliderLabelY[4] = {98.f, 126.f, 154.f, 182.f};
constexpr float kSliderOffset = 12.f;

// checks
constexpr float kChkRowA = 214.f, kChkRowB = 231.f;
constexpr float kChkSize = 0.45f;

// fila de botones
constexpr float kBtnRowY = 262.f;
constexpr float kBtnH = 39.f;
constexpr float kBtnGap = 8.f;
constexpr float kBtnTextScale = 0.6f;

constexpr float kInfoSize = 23.f;
constexpr float kInfoCx = kW - 17.f, kInfoCy = 17.f;

constexpr float centerFromLeft(float left, float width) { return left + width / 2.f; }
constexpr float centerFromRight(float right, float width) { return right - width / 2.f; }

} // namespace gdx::layout
