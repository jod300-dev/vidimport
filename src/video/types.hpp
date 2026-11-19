// ============================================================================
//  vx :: tipos del motor de VIDEO -> Geometry Dash
//  Sin dependencias de Geode (se compila y se prueba en cualquier PC).
//
//  Idea general (distinta a la del GIF Importer):
//    * El video se parte en ESCENAS (cortes de plano).
//    * Cada escena vive en su propio "cuadro" del editor (la CINTA) y la camara estatica de GD salta de uno a otro.
//      GD no dibuja lo que esta fuera de camara, asi que aunque haya 400.000 objetos en el nivel, en pantalla solo hay
//      los de la escena actual: sin lag y sin tener que ocultar/mostrar miles de objetos en cada frame.
//    * Cada escena la codifica uno de los 7 METODOS de animacion (o el Director elige el mejor por escena).
// ============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "core.hpp"

namespace vx {

using gdx::Image;
using gdx::Progress;
using gdx::Result;
using gdx::Shape;

constexpr double kScreenH = 320.0;          // alto de la pantalla de GD a zoom 1 (unidades): un cuadro de video mide 320 u de alto
constexpr int kGroupMax = 9999;             // grupos maximos de GD 2.2
constexpr int kChannelMax = 999;            // canales de color libres: 1..999

enum class Coder : int {
    Reel = 0,        // 1. Cinta de camara
    Mosaic,          // 2. Mosaico vivo
    MosaicDetail,    // 3. Mosaico + detalle
    Epochs,          // 4. Epocas de parches
    Panorama,        // 5. Panoramica + residuo
    Crossfade,       // 6. Fundido cruzado
    PulseMosaic,     // 7. Mosaico de pulsos
    Auto,            // 8. Director automatico
    COUNT
};
constexpr int kCoderCount = int(Coder::COUNT);

struct CoderInfo { const char* name; const char* desc; const char* triggers; };

inline const CoderInfo& coderInfo(Coder c) {
    static const CoderInfo kInfo[kCoderCount] = {
        {"1. Cinta de camara",
         "Cada frame es una imagen completa en su propio sitio del editor y la camara estatica salta de uno a otro. Maxima calidad por frame, cero ocultar/mostrar. Para clips cortos.",
         "Static Camera + Spawn"},
        {"2. Mosaico vivo",
         "Teselas fijas (hasta ~990, una por canal de color) con Color triggers que funden el color entre frames. Muy fluido, muy barato: ideal para videos largos.",
         "Color + Spawn"},
        {"3. Mosaico + detalle",
         "Mosaico vivo de base y, encima, un parche de detalle por frame solo donde el mosaico se queda corto (bordes, caras, texto).",
         "Color + Toggle + Spawn"},
        {"4. Epocas de parches",
         "Un keyframe por epoca y, encima, parches apilados solo de lo que cambia. Una epoca entera se oculta con un solo Toggle. Excelente en planos casi quietos.",
         "Toggle + Spawn"},
        {"5. Panoramica + residuo",
         "Detecta el movimiento de camara, pinta UN panorama del fondo y mueve la camara de GD por el. Solo se pintan los objetos que se mueven.",
         "Static Camera + Toggle + Spawn"},
        {"6. Fundido cruzado",
         "Keyframes espaciados que se mezclan con Alpha triggers: movimiento suave con 2-3 veces menos objetos. Los frames ocultos se desactivan (sin lag).",
         "Alpha + Toggle + Spawn"},
        {"7. Mosaico de pulsos (exp.)",
         "Como el 2, pero cada tesela es un GRUPO y se colorea con Pulse triggers: hasta miles de teselas sin limite de canales. Experimental: prueba con el patron de prueba.",
         "Pulse + Spawn"},
        {"8. Director automatico",
         "Analiza cada escena (movimiento, camara, detalle) y le asigna el metodo 1-7 que mejor rinde con su parte del presupuesto.",
         "segun escena"},
    };
    return kInfo[std::clamp(int(c), 0, kCoderCount - 1)];
}

// Clip ya decodificado y muestreado a la tasa elegida (12 o 30 fps).
struct Clip {
    std::vector<Image> frames;       // un Image por frame de salida (al tamano de trabajo)
    int w = 0, h = 0;
    double fps = 30.0;
    double srcFps = 0.0, srcDuration = 0.0;
    int srcW = 0, srcH = 0;
    std::string name, note;
    int count() const { return int(frames.size()); }
    double seconds() const { return fps > 0 ? double(frames.size()) / fps : 0.0; }
    bool ok() const { return w > 0 && h > 0 && frames.size() >= 2; }
};

struct Params {
    Coder coder = Coder::Auto;
    int fps = 30;                    // 12 o 30
    int budget = 450000;             // objetos totales de la animacion (formas + triggers)
    int maxRaster = 384;             // lado largo de la imagen de trabajo
    int quality = 1;                 // 0 rapido, 1 normal, 2 alto (hibrido con rectangulos girados), 3 maximo
    bool gray = false;
    bool channels = false;           // colores del detalle en canales (paleta) en vez de HSV por objeto
    int paletteK = 96;
    bool smooth = true;              // Color triggers con fundido (interpolacion entre frames)
    bool reassert = true;            // cada paso se lanza dos veces (autocorreccion ante lag)
    int kfStep = 0;                  // fundido cruzado: frames entre keyframes (0 = automatico: 30 fps -> 3, 12 fps -> 1)
    int mosaicMaxCells = 960;
    int pulseMaxCells = 3000;
    int firstGroup = 1;
    std::vector<int> freeChannels;   // canales de color libres del nivel (vacio = 2..999)
    double originX = 0, originY = 2400;   // esquina inferior-izquierda de la zona de la cinta (unidades GD)
    double startX = -30;             // x de los triggers de arranque (negativa = al empezar el nivel)
    int colorChannel = 1010;         // canal base para HSV
    double bleed = 0.15;
    int alphaThr = 128;
    bool flipRotation = false;
    int threads = 4;
    std::vector<int> forced;         // diagnostico: metodo (0..6) forzado por escena
};

struct ShotInfo {
    int a = 0, b = 0;                // frames [a, b)
    Coder coder = Coder::Reel;
    double t0 = 0, dur = 0;          // segundos
    int shapes = 0, triggers = 0, groups = 0;
    double psnr = 0, minPsnr = 0;
    std::string note;
};

struct Output {
    std::vector<std::string> chunks;     // cadenas de GD ya partidas (los triggers van PRIMERO)
    size_t shapes = 0, triggers = 0;
    int groupsUsed = 0, firstGroup = 0, channelsUsed = 0;
    double seconds = 0, psnr = 0, minPsnr = 0, ms = 0;
    std::vector<ShotInfo> shots;
    std::vector<double> framePsnr;       // PSNR de cada frame (lo que se veria en GD frente al original)
    std::string error, note;
    size_t objects() const { return shapes + triggers; }
    bool ok() const { return error.empty() && !chunks.empty(); }
};

inline const char* kShotNames[kCoderCount] = {"Cinta", "Mosaico", "Mos+detalle", "Epocas", "Panoramica", "Fundido", "Pulsos", "Auto"};

} // namespace vx
