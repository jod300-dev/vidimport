# v1.0.2-beta
- **Duracion hasta 500 s**: el deslizador de *Duracion* llega ahora hasta 500 s (antes lo frenaba solo el presupuesto). Sigue limitado por los **9999 grupos** de GD (hacen falta ~2 por frame): a 12 fps caben ~415 s y a 30 fps ~166 s; la nota de la ventana dice cual de los dos topes te frena.
- **El bucle dura EXACTAMENTE lo que el MP4**, a 12 y a 30 fps: antes la vuelta duraba `frames / fps` (se perdia hasta 1 frame por vuelta, p. ej. 10.03 s -> 10.00 s) y el numero de frames se truncaba. Ahora el numero de frames se redondea al mas cercano y `Spawn(M, total)` usa la duracion real del tramo; el ultimo frame dura lo que reste.
- Ajuste **Correccion de velocidad (x)** (1.0 por defecto): multiplica todos los retardos. Si en tu GD la animacion dura menos que el video, sube el valor (p. ej. 1.05).
- El **play-log** mide ahora el reloj del juego frente al real (`reloj juego/real`) cada 5 s y el resumen de la colocacion anota la duracion del bucle: sirve para ver si GD avanza a la par que el tiempo real.
- Los triggers ocupan **50 bloques mas en X y en Y** (ajustes *Triggers: ampliar en X / en Y*, 50 por defecto): rejilla de 130 columnas y paso vertical mayor, sin celdas repetidas.

# v1.0.1-beta
- Diagnostico de "Jugar no entra": el log (`play-log.txt`) ahora anota **cada cambio de escena** mientras se carga o se juega (`ESCENA -> ...`: a que pantalla va GD y cuantos ms despues de cargar), y los **5 primeros fotogramas** tras cargar con su dt. Antes, una salida a la pantalla de editar nombre no dejaba ninguna huella.

# v1.0.0-beta
- El log al jugar ahora tambien mide el lag dentro del nivel (fps, peor fotograma, picos con su instante) y registra como termina la partida.
- Ajuste **Guardar log al jugar**: escribe `play-log.txt` (tamano del nivel, objetos, X maxima, resultado de `PlayLayer::init`, final del log de Geode) al pulsar Jugar / Guardar y jugar y si GD vuelve a la pagina del nivel sin cargar.
- Anti-lag: opcion **cada parte a distinta distancia** (paso y distancia maxima en Ajustes; cada Move con su distancia). Distancia de espera por defecto 30000.
- **Partes anti-lag**: los frames se reparten en partes; solo una esta cerca de la camara y las demas esperan lejos hasta que un Move las trae (ajustes: objetos por parte, anticipacion, distancia).
- Casilla **Canales** (sin HSV): paleta unica para todo el video + Color triggers al inicio.
- Los MP4 **fragmentados** ya se leen (antes: "No se pudo leer la duracion del video").
- El deslizador de **Duracion** llega mucho mas lejos (40 obj/frame o 9999 grupos) con paso adaptativo; al abrir se propone la duracion con buena calidad y se avisa de calidad baja.
- Icono y logo propios (ya no son los del GIF Importer).
- Casilla **Canales (sin HSV)**: paleta global del video (8-900 colores) en canales 2.., fijada por Color triggers al inicio; la paleta cuenta en el presupuesto.
- Primera version. Motor de conversion reciclado de GIF Importer / Image Exporter (BSP, hibrido, quadtree, piramide, regiones, franjas, rotados, cuadricula + exportador de objetos y colocacion por tandas).
- Decodificacion de MP4/MOV/M4V: Media Foundation (Windows), AMediaCodec (Android), AVFoundation (macOS/iOS) y ffmpeg como respaldo en PC.
- Muestreo a 12 o 30 fps (frame mas cercano en el tiempo), rotacion de moviles en vertical.
- Modos de reproduccion Toggle (lista con instantes absolutos) y Spawn (cadena con resincronizacion cada segundo).
- Presupuesto total de objetos (por defecto 450000): reparto adaptativo, frames repetidos reutilizados, estabilizacion temporal del ruido de compresion.
