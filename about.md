# Video Importer (beta)

Convierte un **MP4** en una animacion del editor de Geometry Dash. Cada frame se convierte en objetos y se reproduce con **Toggle triggers + Spawn triggers** (nada mas: ni Color, ni Alpha, ni Pulse), en **bucle infinito**.

- **12 fps o 30 fps** (dos casillas): a que ritmo se muestrea el video.
- **Toggle (exacto) o Spawn (ligero)**: dos formas de encadenar los frames.
- **Presupuesto de objetos** de hasta **500.000** para TODA la animacion (frames + triggers), repartido segun el detalle de cada frame; los frames casi iguales se reutilizan.
- Los frames ocultos quedan **desactivados** (Toggle): GD ni los dibuja ni los procesa.
- Decodifica con el sistema (Windows: Media Foundation, Android: MediaCodec, macOS/iOS: AVFoundation); en PC, ffmpeg como respaldo.

En el editor los frames se ven superpuestos: **juega el nivel** para ver el video. Guarda antes de importar (no se puede deshacer). Con 400-500k objetos hace falta un editor **sin limite de objetos**.
