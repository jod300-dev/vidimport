# v1.0.0-beta
- Primera version. Motor de conversion reciclado de GIF Importer / Image Exporter (BSP, hibrido, quadtree, piramide, regiones, franjas, rotados, cuadricula + exportador de objetos y colocacion por tandas).
- Decodificacion de MP4/MOV/M4V: Media Foundation (Windows), AMediaCodec (Android), AVFoundation (macOS/iOS) y ffmpeg como respaldo en PC.
- Muestreo a 12 o 30 fps (frame mas cercano en el tiempo), rotacion de moviles en vertical.
- Modos de reproduccion Toggle (lista con instantes absolutos) y Spawn (cadena con resincronizacion cada segundo).
- Presupuesto total de objetos (por defecto 450000): reparto adaptativo, frames repetidos reutilizados, estabilizacion temporal del ruido de compresion.
