# Video Importer (beta) — MP4 a Geometry Dash

Mod de Geode que convierte un MP4 en objetos del editor y lo anima en bucle infinito usando **solo Toggle triggers y Spawn triggers** para la animacion (el color va en HSV, o en canales con la casilla *Canales*).

## Uso
1. Abre el editor, pulsa el boton del importador (barra *Edit*).
2. **Video** -> elige el MP4 (H.264 siempre funciona; HEVC/VP9/AV1 dependen del sistema).
3. Elige el metodo (flechas), **Objetos totales**, **Lado largo**, **Inicio** y **Duracion**, **12 fps / 30 fps**, **Toggle / Spawn** y, si quieres, **Grises**, **Canales** y **Partes anti-lag**.
4. **Colocar**. Se decodifica, se convierte en paralelo y se coloca por tandas (sin congelar el editor). Luego **juega el nivel**.

Los 2 triggers de arranque van en x = -30. Si hay un unico objeto seleccionado, el video se centra en el; si no, en el centro de la vista.

## Como esta hecho (y que se recicla de los dos mods)
| Pieza | Origen |
|---|---|
| Motor de conversion (BSP, hibrido, quadtree, piramide, regiones, franjas, rotados, cuadricula), saneado, exportador de objetos HSV, z-order, colocacion por tandas, ventana, `Layout`/`FitText`, boton del editor, CI | GIF Importer / Image Exporter |
| `src/video/` decodificacion (Media Foundation / NDK / AVFoundation / ffmpeg), lector de cabecera MP4, muestreador 12/30 fps | nuevo |
| `src/engine/videoplan.hpp` presupuesto total, reparto adaptativo, frames repetidos, estabilizacion temporal | nuevo |
| `src/engine/videoanim.hpp` triggers Toggle y Spawn | nuevo |

No se reciclaron los metodos por capas/LSA/tinta: usan Color o Alpha triggers para animar y aqui la animacion solo usa Toggle y Spawn.

## Colores: HSV o Canales
- **HSV** (por defecto): cada objeto lleva su color en HSV sobre el canal 1010. Colores ilimitados, cero triggers de color.
- **Canales** (casilla): UNA paleta global para todo el video (corte mediano + Lloyd, ponderada por area; por defecto 128 colores, ajustable 8-900). Cada objeto lleva solo `21,<canal>` (canales 2 en adelante, nunca el 1) y un **Color trigger por canal, una sola vez al inicio del nivel** fija su color. La cadena pesa ~33 % menos (GD carga mas rapido); a cambio hay menos colores (en las pruebas, 128 colores = -0,6 dB frente a HSV; 16 colores = -4,6 dB) y se ocupan los canales 2..K+1 (si tu nivel ya los usa, sus colores cambiarian). Los Color triggers cuentan como objetos del presupuesto. Si desactivas *Arrancar al inicio del nivel*, se colocan junto al video y se ejecutan al llegar a ellos.

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

## Partes anti-lag (casilla *Partes anti-lag (Move)*)
Con 400-500k objetos, aunque los frames ocultos esten desactivados, GD sigue teniendo ~450k objetos en las mismas secciones. Con esta casilla los frames se agrupan en **partes** de ~25000 objetos (Ajustes: *objetos por parte*) y solo una esta cerca de la camara:
1. Todas las partes menos la primera **esperan juntas y lejos en X** (3000 unidades por defecto; sin Toggle de parte: solo la distancia las deja fuera de juego).
2. Un **Move instantaneo (-D, 0)** trae la parte siguiente `anticipacion` segundos (0,5 s) ANTES de su primer frame; sus frames siguen ocultos por Toggle, asi que no se ve.
3. Cuando la parte termina y su ultimo frame ya esta oculto, otro **Move (+D, 0)** la devuelve lejos. Entra la siguiente y asi hasta cerrar la vuelta; el bucle repite el ciclo sin deriva.

Hay un Move por grupo de frames y por sentido, dentro de los pasos que ya existian (ningun grupo nuevo). Como los Move son **relativos**, los pasos que llevan Move se ejecutan una sola vez por vuelta (en modo Toggle no llevan la segunda ejecucion de reafirmacion). La parte que empieza la vuelta (y la que termina en la siguiente) se coloca en el origen; el resto, lejos: en el editor solo veras la primera parte.

*Objetos por parte* es el control de todo: **25000** = pocos Move grandes; **~1 frame** (p. ej. 1000) = flujo continuo de Move pequenos, sin picos y con muy pocos objetos cerca de la camara.

**Medido en el simulador de GD** (120k objetos, 20 s a 12 fps; imagen identica con y sin partes, 3 vueltas sin deriva, Toggle y Spawn, con lag de tick 1/60 y orden inverso):

| Objetos por parte | Objetos cerca de la camara (max) | Mayor Move en un tick |
|---|---|---|
| sin partes | 117.698 | - |
| 15.000 | 30.548 (-74 %) | 15.407 |
| 3.000 (lead 0,25 s) | 6.841 (-94 %) | 3.484 |
| ~1 frame (500) | 4.882 (-96 %) | 1.510 |

**Lo que NO esta verificado** (solo se puede ver en el juego): que reduzca de verdad el lag (es la hipotesis: GD recorre los objetos cercanos aunque esten desactivados), que un Move de 25k objetos instantaneo no de un tiron apreciable, y que GD mueva objetos cuya parte esta desactivada por Toggle (creo que si). La carga del nivel y la memoria no cambian. Los Move son un trigger mas que Toggle/Spawn. Si notas tirones al entrar una parte: sube *anticipacion*, baja *objetos por parte* o usa partes de ~1 frame.

## Color: HSV o Canales
- **HSV** (por defecto): cada objeto lleva su color en HSV sobre un canal base; 16 millones de colores, cero triggers de color.
- **Canales** (casilla *Canales*): una **paleta unica para todo el video** (corte mediano + Lloyd ponderado por area, 128 colores por defecto, ajustable hasta 900 en Ajustes). Los objetos usan solo `21,<canal>` (canales 2 en adelante; el 1 nunca) y el color de cada canal lo fija un **Color trigger al inicio del nivel** (uno por canal, una sola vez). La cadena pesa ~33 % menos y GD carga mas rapido; a cambio hay menos colores (en las pruebas, 128 colores = -0,6 dB frente a HSV; 16 colores = -4,6 dB). Los Color triggers cuentan dentro del presupuesto de objetos. La animacion en si sigue usando solo Toggle + Spawn.

## Presupuesto de objetos
`objetos totales = frames distintos x objetos por frame + triggers`, con tope en **Objetos totales** (por defecto 450000) y en el **Limite de objetos del editor**. El deslizador de **Duracion** llega hasta donde caben **40 objetos por frame** (suelo absoluto) o los **9999 grupos** de GD (~2 por frame: unos 4990 frames, ~415 s a 12 fps o ~165 s a 30 fps). Al abrir un video se propone la duracion con buena calidad (*Objetos por frame recomendados* en Ajustes, 200 por defecto) y la nota avisa de **calidad baja** si estiras mas. Probado: 400 s a 12 fps = 4800 frames -> 414k objetos + 28,8k triggers = 442,8k. Medido en las pruebas: 40 s a 30 fps = 1200 frames -> 441.0k objetos + 7.2k triggers = 448.2k (el calculo de triggers es exacto).

> GD 2.2 de serie solo admite unos ~200000 objetos en el editor (dato de memoria, sin verificar en tu version). Para 400-500k necesitas un mod que quite ese limite. Si GD crea menos objetos de los pedidos, el mod lo avisa al terminar.

## Que esta verificado y que no
Probado en un PC (g++ 13 + ffmpeg), sin Geode ni juego (`tests/run_tests.sh`):
- Lector de cabecera MP4/MOV contra `ffprobe` (duracion, fps, codec, rotacion), **incluidos MP4 fragmentados** (OBS, navegadores, grabadores de pantalla: `moof`/`trun`).
- Muestreo a 12 y 30 fps (numero de frames exacto, rotacion, recorte por inicio/duracion).
- MP4 -> plan -> objetos -> cadena -> **simulador de GD**: cada frame sale en su sitio en Toggle y en Spawn, con tick de 1/60 y 1/30 s, orden de ejecucion invertido y empates aleatorios; el numero de triggers real coincide con el calculado.
- Conversion YUV->RGBA de Android contra ffmpeg (BT.601 / BT.709, limitado / completo).
- Modo Canales: paleta global + Color triggers de arranque simulados (128 y 16 colores, Toggle y Spawn); los triggers reales coinciden con el calculo (+ los de la paleta).
- `VideoPopup.cpp` compila contra un mock minimo de Geode.

**NO probado** (necesita el CI de GitHub y un dispositivo): la compilacion y el funcionamiento de `decode_win.cpp` (Media Foundation), `decode_android.cpp` (AMediaCodec: formatos de color no estandar de algunos telefonos darian error claro) y `decode_apple.mm`; la compilacion de `main.cpp`/`VideoPopup.cpp` contra el Geode real; y el comportamiento real de GD con 400-500k objetos (carga, memoria). Si el CI falla, pega el error y se corrige.

## Limites honestos
- Solo se usa el objeto de bloque que ya usaban los otros mods (id 3092) con escala, giro, z-order y color HSV por objeto. No se anadieron triangulos/pendientes ni otros objetos porque no puedo verificar sus ids sin el juego.
- Videos HDR/10 bits o HEVC pueden no decodificarse segun el sistema.
- Si un video no se abre ("no se pudo leer la duracion"), re-exportalo: `ffmpeg -i in.mp4 -c copy -movflags +faststart out.mp4`.
- Rutas con caracteres no ASCII: soportado en Windows (UTF-8 -> UTF-16) y en el resto por defecto.

## Iconos
`res/gdVideoBtn.png` (boton del editor, 160x160) y `logo.png` (336x336) se generan con `tools/make_icons.py` (Pillow): fotograma de pelicula con perforaciones, atardecer troceado en rectangulos (guino al metodo BSP), boton de reproduccion y etiqueta MP4.

## Compilar
Como los otros mods: Geode SDK + `GEODE_SDK`, o el workflow `.github/workflows/build.yml` (Windows, macOS, iOS, Android 32/64).
