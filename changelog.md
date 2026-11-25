# v1.0.5-beta
- **Nuevo ajuste "Anti-kick (test)"** (apagado por defecto). En el ejecutable de GD 2.2081 se ve que `PlayLayer::setupHasCompleted` llama a `GJBaseGameLayer::shouldExitHackedLevel` y, si da verdadero, saca del nivel (`removeAllObjects` + `onQuit`). Esa funcion cuenta los objetos de cada celda de **100x100 u** (indice = `trunc(pos * 0.01f)`, 0 si pos <= 0) segun su posicion INICIAL: pasa de **8000** objetos en una celda -- de **4000** si hay mas de 0.9 celdas vacias por cada celda con objetos -- y te expulsa. Con el check activo el mod **simula esas celdas** antes de colocar; fuerza las partes anti-lag (cada parte a distinta distancia, aunque el anti-lag este apagado) y **reduce los objetos por parte** hasta que ninguna celda pase de **3400** (margen sobre el limite de 4000). Si no lo consigue, el mensaje avisa (RIESGO DE EXPULSION) y el log de la colocacion anota celda maxima, limite y proporcion de celdas vacias. La animacion no cambia: solo la colocacion inicial.
- Con anti-kick el presupuesto de objetos y de grupos cuenta los Move del anti-lag (en Remap, 4 triggers y 3 grupos por parte).

# v1.0.4-beta
- **El maximo del deslizador de Duracion ya no depende del deslizador "Objetos totales"** (por eso se quedaba en ~216 s a 12 fps y ~166 s a 30 fps): ahora llega hasta lo que permiten los 9999 grupos de GD y el techo de objetos (ajuste y editor), con un tope de 500 s. Si la duracion que eliges necesita mas objetos, el total **sube solo** (hasta ese techo) para mantener los obj/frame minimos; si bajas el total a mano, la duracion se acorta como antes.
- Con **Remap (mini)** los topes son ~500 s a 12 fps y ~330 s a 30 fps (9999 grupos menos los que ya use tu nivel); con Toggle/Spawn siguen siendo ~2 grupos por frame (~415 s y ~166 s).
- La nota de la ventana, cuando la duracion esta al maximo, dice QUE limita: grupos de GD (y desde que grupo libre empieza), objetos (y el techo) o los 500 s.

# v1.0.3-beta
- **Nuevo check "Remap (mini)"**, el metodo mas ligero. Una unica plantilla de 4 Toggle (mostrar 1 + ocultar 3, con 4 grupos "pin" sin objetos) se reutiliza con **remap de Spawn** (clave 442): M lanza `Spawn(plantilla, t, "pin.G_e.pin.G_a...")` por paso. Casi **1 trigger y 1 grupo por frame** (Toggle/Spawn: ~6 y 2): con los 9999 grupos de GD caben ~9990 frames (**500 s a 12 fps**, ~333 s a 30 fps) y sobran ~80 % de triggers para la imagen (en las pruebas: 338 -> 71 triggers). Instantes absolutos como Toggle (una sola ejecucion por paso) y el mismo `Spawn(M, duracion)`.
- **El anti-lag sigue funcionando con Remap**: cada parte tiene un grupo propio y UN Move de entrada y UNO de salida mueven la parte entera (los frames llevan 2 grupos: el suyo y el de su parte). Mismas distancias, lead y "cada parte a distinta distancia".

# v1.0.2-beta
- **Duracion hasta 500 s**: el deslizador de *Duracion* llega ahora hasta 500 s (antes lo frenaba solo el presupuesto). Sigue limitado por los **9999 grupos** de GD (hacen falta ~2 por frame): a 12 fps caben ~415 s y a 30 fps ~166 s; la nota de la ventana dice cual de los dos topes te frena.
- **El bucle dura EXACTAMENTE lo que el MP4**, a 12 y a 30 fps: antes la vuelta duraba `frames / fps` (se perdia hasta 1 frame por vuelta, p. ej. 10.03 s -> 10.00 s) y el numero de frames se truncaba. Ahora el numero de frames se redondea al mas cercano y `Spawn(M, total)` usa la duracion real del tramo; el ultimo frame dura lo que reste.
- Ajuste **Correccion de velocidad (x)** (1.0 por defecto): multiplica todos los retardos. Si en tu GD la animacion dura menos que el video, sube el valor (p. ej. 1.05).
- El **play-log** mide ahora el reloj del juego frente al real (`reloj juego/real`) cada 5 s y el resumen de la colocacion anota la duracion del bucle: sirve para ver si GD avanza a la par que el tiempo real.
- Los triggers siguen **pegados** (paso de 1 bloque, uno al lado y uno encima del otro) y la rejilla es **50 columnas mas ancha** (130; ajuste *Triggers: columnas extra*): las filas suben las que hagan falta, sin tope.
- **Tope de objetos: 1 millon** (antes 500000): *Tope total de objetos* y *Limite de objetos del editor* llegan a 1000000 y es su valor por defecto.

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
