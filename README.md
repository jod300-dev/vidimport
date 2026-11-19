# Video Importer (mod de Geode) — BETA

Convierte un video en objetos + triggers de Geometry Dash que lo reproducen en bucle. Reutiliza el motor de pintado del *Image Exporter* / *GIF Importer* (BSP, hibrido con rectangulos girados, paleta, exportacion de objetos, colocacion por tandas, estructura de la ventana) y añade un motor de **video** propio.

## Idea central: la cinta de camara

Un nivel de 450.000 objetos solo va fluido si en pantalla hay pocos. Por eso:

1. El video se parte en **escenas** (cortes de plano).
2. Cada escena vive en **su propio cuadro** del editor (zona alta del nivel, por defecto y = 2400, donde el jugador nunca llega) separado de los demas por huecos > 840 u.
3. Una **Static Camera** salta de un cuadro a otro. GD no dibuja lo que esta fuera de camara, asi que solo se renderiza la escena actual.
4. El **trigger de camara es su propio ancla**: pertenece a un grupo y apunta a ese grupo, asi que la camara se centra en la posicion del trigger sin objetos extra.
5. Todo es **idempotente** (cada paso fija un estado absoluto) y se relanza ~10-20 ms despues (ajuste *Relanzar cada paso*).

El cuadro de video mide **320 u de alto** (la pantalla de GD a zoom 1; maximo 560 de ancho para que entre en 16:9).

## Estructura de triggers

| Grupo | Contenido |
|---|---|
| paso `S_f` | triggers de un instante (camara, Toggle, Alpha, Color, Pulse) — "spawn triggered" |
| segmento `L_k` | hasta 100 `Spawn(S_f, retardo)` (los retardos son relativos al inicio del segmento) |
| maestro `M` | `Spawn(L_k, tau_k)` + `Spawn(M, duracion total)` → **bucle infinito** |
| arranque | 1 Spawn de posicion en x = -30 que lanza `M` |

Los triggers se colocan en la zona del video (a la izquierda), nunca en el nivel del jugador. En la salida, **los triggers van primero**: si GD cortase la cadena, se perderian formas, no la animacion.

## Los 8 metodos

| # | Metodo | Que hace | Triggers |
|---|---|---|---|
| 1 | **Cinta de camara** | cada frame es una imagen completa en su cuadro; la camara salta. Maxima calidad por frame, nada que ocultar/mostrar. Los frames repetidos reutilizan el cuadro | Static Camera, Spawn |
| 2 | **Mosaico vivo** | teselacion *temporal* adaptativa (hasta ~990 teselas que minimizan el error sobre TODOS los frames de la escena), una tesela = un canal de color. Cada frame solo cambia las teselas que difieren (umbral automatico segun presupuesto). Con **fundido**: el Color trigger dura todo el intervalo y termina justo en el instante del frame → interpolacion temporal | Color, Spawn |
| 3 | **Mosaico + detalle** | mosaico de base + por frame un parche de *residuo* (solo donde el mosaico se queda corto), intercambiado con Toggle | Color, Toggle, Spawn |
| 4 | **Epocas de parches** | un keyframe por epoca y parches apilados (z creciente) con solo lo que cambia. No hay que ocultar nada por frame: una epoca entera se apaga con **un** Toggle | Toggle, Spawn |
| 5 | **Panoramica + residuo** | estima el movimiento global de camara (busqueda gruesa 1/4 + refinado a pixel), construye UN panorama del fondo (mediana temporal: quita lo que se mueve), lo pinta una vez y **mueve la camara de GD** por el. Solo se pintan los objetos que se mueven | Static Camera, Toggle, Spawn |
| 6 | **Fundido cruzado** | keyframes espaciados (30 fps: cada 3; 12 fps: cada 1) que se mezclan con Alpha; el fundido *termina* en el instante del keyframe y el anterior se **desactiva** con Toggle (nunca hay mas de 2 capas activas) | Alpha, Toggle, Spawn |
| 7 | **Mosaico de pulsos** (experimental) | como el 2 pero cada tesela es un **grupo** (hasta ~9000 en vez de 990 canales) y se colorea con Pulse sobre grupo (exclusivo, hold largo) | Pulse, Spawn |
| 8 | **Director automatico** | analiza cada escena (movimiento, movimiento de camara, residuo tras compensarla, detalle, presupuesto por frame) y asigna 1-7 | segun escena |

### Presupuesto

* El presupuesto (formas + triggers) se reparte entre escenas segun *frames que cuestan* × detalle. Las escenas con frames repetidos cuestan casi nada.
* Se procesan **primero las baratas**: lo que no gastan pasa a las caras (reparto dinamico).
* Los metodos 4, 5 y 3 usan un **controlador de presupuesto en lazo cerrado** (el error de un frame lo corrige el siguiente) y nunca lo superan; se deja un 1,5 % de margen.
* Los mosaicos (2, 3, 7) buscan automaticamente el umbral de cambio mas fino que cabe; si no cabe, reducen el numero de teselas.

### 12 fps / 30 fps

Los dos checks son excluyentes y fijan la tasa de muestreo. A 12 fps hay 2,5 veces menos frames: mas presupuesto por frame y, con *Fundir colores*/*Fundido cruzado*, aspecto fluido. A 30 fps el cambio de frame (33 ms) esta justo en el limite de lo que GD resuelve de forma fiable (el GIF Importer usa un minimo de 0,03 s).

## Origen de video

| Plataforma | MP4/MOV/MKV/WEBM | GIF | Carpeta PNG/JPG |
|---|---|---|---|
| Windows | **Media Foundation** (nativo, `source_wmf.cpp`), ffmpeg de respaldo | si | si |
| macOS / Linux | ffmpeg externo (PATH o ajuste) | si | si |
| Android / iOS | **no incluido** (convierte a GIF o carpeta de imagenes) | si | si |

## Estructura

```
src/engine/   motor reciclado (core, partition, regions, geometrize, mono, palette, refit, gdexport, engine, gifdecode)
src/video/    types, out (cadenas GD), analysis (escenas, movimiento, teselacion), paint, coders (7 metodos), director, source (+_wmf)
src/ui/       VideoPopup (ventana)
tests/        vtest / vcheck / vreal + vsim.hpp (simulador del modelo de triggers)  ->  ./tests/run_tests.sh
```

## Que esta verificado y que no

**Verificado en un PC (sin Geode)**: el motor compila con g++ y los 8 metodos producen cadenas coherentes; un **simulador** propio (`tests/vsim.hpp`: Spawn, Toggle, Alpha con fundido, Color con fundido, Pulse, Static Camera, z-order, grupos) reproduce cada animacion y lo que "se ve" coincide con el original — mismo PSNR que calcula el codificador, 12 y 30 fps, y **sin cuadros vecinos visibles** en pantallas hasta 21:9. Con un MP4 real (cortes, 1280×720): 60 s a 30 fps y 450.000 objetos se calculan en ~4 s; el presupuesto no se supera.

**No verificado (no hay Geode ni GD en el sandbox)**:
* La compilacion de `VideoPopup.cpp`, `main.cpp` y `source_wmf.cpp` (se escribieron siguiendo exactamente la API que ya compila en el GIF Importer, pero no se han compilado).
* El comportamiento real en GD de: Static Camera que apunta a su propio grupo (ancla), clave 56 del Toggle, claves 45/46/47/52/86 del Pulse, Alpha con fundido sobre objetos desactivados y Color con fundido. El simulador modela lo que el mod *espera*; por eso existe el boton **Prueba**.
* El limite de objetos del editor: el GIF Importer asumia 200.000 (GD sin modificar). Para 400-500 mil tu GD debe permitirlo; el ajuste *Limite de objetos del editor* (500.000 por defecto) hace que la colocacion se detenga antes de pasarse.

## Compilar

Igual que el GIF Importer: `GEODE_SDK` definido, `geode build` (o el workflow de `.github/workflows/build.yml`).
