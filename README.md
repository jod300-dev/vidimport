# Video Importer (beta) — MP4 a Geometry Dash

Mod de Geode que convierte un MP4 en objetos del editor y lo anima en bucle infinito usando **solo Toggle triggers y Spawn triggers**.

## Uso
1. Abre el editor, pulsa el boton del importador (barra *Edit*).
2. **Video** -> elige el MP4 (H.264 siempre funciona; HEVC/VP9/AV1 dependen del sistema).
3. Elige el metodo (flechas), **Objetos totales**, **Lado largo**, **Inicio** y **Duracion**, **12 fps / 30 fps** y **Toggle / Spawn**.
4. **Colocar**. Se decodifica, se convierte en paralelo y se coloca por tandas (sin congelar el editor). Luego **juega el nivel**.

Los 2 triggers de arranque van en x = -30. Si hay un unico objeto seleccionado, el video se centra en el; si no, en el centro de la vista.

## Como esta hecho (y que se recicla de los dos mods)
| Pieza | Origen |
|---|---|
| Motor de conversion (BSP, hibrido, quadtree, piramide, regiones, franjas, rotados, cuadricula), saneado, exportador de objetos HSV, z-order, colocacion por tandas, ventana, `Layout`/`FitText`, boton del editor, CI | GIF Importer / Image Exporter |
| `src/video/` decodificacion (Media Foundation / NDK / AVFoundation / ffmpeg), lector de cabecera MP4, muestreador 12/30 fps | nuevo |
| `src/engine/videoplan.hpp` presupuesto total, reparto adaptativo, frames repetidos, estabilizacion temporal | nuevo |
| `src/engine/videoanim.hpp` triggers Toggle y Spawn | nuevo |

No se reciclaron los metodos por capas/LSA/tinta: usan Color o Alpha triggers y aqui solo se permiten Toggle y Spawn.

## 12 fps / 30 fps
El video se muestrea en `t = inicio + k / fps` eligiendo el frame decodificado **mas cercano** (igual que el filtro `fps` de ffmpeg). Con el mismo presupuesto, 12 fps da ~2,5 veces mas objetos por frame y videos ~2,5 veces mas largos; 30 fps es mas fluido.

## Toggle vs Spawn
Los dos usan el mismo z-order (creciente en el tiempo: el frame nuevo tapa del todo al anterior, asi un "ocultar" perdido no se ve) y los mismos pasos `F_e` (mostrar el nuevo, ocultar los 2 anteriores y 1 lejano, repartido por el bucle). Solo cambia como se encadenan:

| | **Toggle (exacto)** | **Spawn (ligero)** |
|---|---|---|
| Encadenado | `M` lanza TODOS los pasos en su instante absoluto (+ una segunda ejecucion idempotente) | cada paso lanza al siguiente; `M` solo lanza la cabeza de cada bloque de 1 s |
| Precision | maxima, nunca se acumula error | resincroniza cada bloque (ajustable) |
| Triggers (medido) | ~7 por frame | ~6 por frame |
| Carga | todos los Spawn se encolan al empezar cada vuelta | carga constante, sin pico |
| Si GD pierde un Spawn | se repara solo | la cadena se repara en el siguiente bloque |

## Presupuesto de objetos
`objetos totales = frames distintos x objetos por frame + triggers`, con tope en **Objetos totales** (por defecto 450000) y en el **Limite de objetos del editor**. La duracion maxima de la ventana sale de `minimo por frame` (Ajustes, 200 por defecto). Medido en las pruebas: 40 s a 30 fps = 1200 frames -> 441.0k objetos + 7.2k triggers = 448.2k (el calculo de triggers es exacto).

> GD 2.2 de serie solo admite unos ~200000 objetos en el editor (dato de memoria, sin verificar en tu version). Para 400-500k necesitas un mod que quite ese limite. Si GD crea menos objetos de los pedidos, el mod lo avisa al terminar.

## Que esta verificado y que no
Probado en un PC (g++ 13 + ffmpeg), sin Geode ni juego (`tests/run_tests.sh`):
- Lector de cabecera MP4 contra `ffprobe` (duracion, fps, codec, rotacion).
- Muestreo a 12 y 30 fps (numero de frames exacto, rotacion, recorte por inicio/duracion).
- MP4 -> plan -> objetos -> cadena -> **simulador de GD**: cada frame sale en su sitio en Toggle y en Spawn, con tick de 1/60 y 1/30 s, orden de ejecucion invertido y empates aleatorios; el numero de triggers real coincide con el calculado.
- Conversion YUV->RGBA de Android contra ffmpeg (BT.601 / BT.709, limitado / completo).
- `VideoPopup.cpp` compila contra un mock minimo de Geode.

**NO probado** (necesita el CI de GitHub y un dispositivo): la compilacion y el funcionamiento de `decode_win.cpp` (Media Foundation), `decode_android.cpp` (AMediaCodec: formatos de color no estandar de algunos telefonos darian error claro) y `decode_apple.mm`; la compilacion de `main.cpp`/`VideoPopup.cpp` contra el Geode real; y el comportamiento real de GD con 400-500k objetos (carga, memoria). Si el CI falla, pega el error y se corrige.

## Limites honestos
- Solo se usa el objeto de bloque que ya usaban los otros mods (id 3092) con escala, giro, z-order y color HSV por objeto. No se anadieron triangulos/pendientes ni otros objetos porque no puedo verificar sus ids sin el juego.
- Videos HDR/10 bits o HEVC pueden no decodificarse segun el sistema.
- Rutas con caracteres no ASCII: soportado en Windows (UTF-8 -> UTF-16) y en el resto por defecto.

## Compilar
Como los otros mods: Geode SDK + `GEODE_SDK`, o el workflow `.github/workflows/build.yml` (Windows, macOS, iOS, Android 32/64).
