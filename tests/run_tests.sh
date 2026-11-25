#!/usr/bin/env bash
# Pruebas del mod SIN Geode ni juego (Linux/macOS, g++ >= 13 y ffmpeg para generar videos de prueba).
#   probe_test        el lector de cabecera MP4 (duracion, fps, codec, rotacion)
#   sample_test       el muestreador a 12 / 30 fps con el backend de ffmpeg
#   e2e_test          MP4 -> frames -> plan -> objetos -> cadena de GD -> simulador de GD -> PSNR (Toggle y Spawn, con lag/orden inverso/triggers perdidos)
#   yuv_android_test  la conversion YUV->RGBA que usa el backend de Android
#   ui compile check  VideoPopup.cpp contra un mock minimo de Geode (cazar errores de tipos)
set -e
cd "$(dirname "$0")"
W=$(mktemp -d); S=../src; CXX="g++ -std=c++23 -O2 -pthread -I$S -I$S/engine -I$S/video -I."
echo "== videos de prueba"; ffmpeg -v error -y -f lavfi -i testsrc2=size=640x360:rate=30 -t 4 -c:v libx264 -pix_fmt yuv420p $W/a.mp4
ffmpeg -v error -y -f lavfi -t 1.5 -i testsrc2=size=640x360:rate=30 -vf tpad=stop_mode=clone:stop_duration=2.5 -c:v libx264 -pix_fmt yuv420p $W/frozen.mp4
ffmpeg -v error -y -f lavfi -i testsrc2=size=640x360:rate=30 -t 5 -c:v libx264 -pix_fmt yuv420p -movflags frag_keyframe+empty_moov $W/frag.mp4
echo "== probe";  $CXX probe_test.cpp -o $W/probe && $W/probe $W/a.mp4 && $W/probe $W/frag.mp4
echo "== sample"; $CXX sample_test.cpp $S/video/decode_stub.cpp $S/video/decode_ffmpeg_cli.cpp -o $W/sample && $W/sample $W/a.mp4 12 && $W/sample $W/a.mp4 30 1 2
echo "== e2e";    $CXX e2e_test.cpp $S/video/decode_stub.cpp $S/video/decode_ffmpeg_cli.cpp -o $W/e2e
for m in 0 1 2; do $W/e2e $W/a.mp4 12 $m 40000 2 | grep -E "^N=|variante"; done
echo "-- modo Canales (paleta de 128 colores):"; for m in 0 1 2; do CHAN=128 $W/e2e $W/a.mp4 12 $m 40000 2 | grep -E "^N=|variante 0"; done
echo "-- con frames repetidos (deben detectarse):"; $W/e2e $W/frozen.mp4 30 0 450000 2 | grep -E "^N="
echo "== anti-lag (partes): imagen identica, objetos cerca de la camara, Move, deriva"; $CXX lag_test.cpp $S/video/decode_stub.cpp $S/video/decode_ffmpeg_cli.cpp -o $W/lag
ffmpeg -v error -y -f lavfi -i mandelbrot=size=640x360:rate=12 -t 20 -c:v libx264 -pix_fmt yuv420p $W/m20.mp4
for m in 0 1 2; do $W/lag $W/m20.mp4 12 $m 120000 15000 20 | tail -2; done
$W/lag $W/m20.mp4 12 0 120000 500 20 | tail -2; $W/lag $W/m20.mp4 12 2 120000 500 20 | tail -2
echo "-- partes a distinta distancia (paso 30000 y 10000):"; SPREAD=30000 $W/lag $W/m20.mp4 12 0 120000 15000 20 | grep -E "mitad|OK|FALLO"; SPREAD=10000 $W/lag $W/m20.mp4 12 1 120000 3000 20 | grep -E "mitad|OK|FALLO"; SPREAD=10000 $W/lag $W/m20.mp4 12 2 120000 3000 20 | grep -E "mitad|OK|FALLO"
echo "== tiempo del bucle (= duracion del video a 12 y 30 fps, con escala) y rejilla de triggers (+50 bloques en X e Y)"; $CXX timing_test.cpp $S/video/decode_stub.cpp -o $W/timing && $W/timing | tail -3
echo "== anti-kick (celdas de 100x100 u como las mira GD 2.2081)"; $CXX kick_test.cpp $S/video/decode_stub.cpp -o $W/kick && $W/kick | tail -8
echo "== anti-kick + anti-lag / e2e (misma imagen que sin anti-kick): limite exigido pequeno para que las partes entren en juego"
for m in 0 1 2; do KICK=400 $W/lag $W/m20.mp4 12 $m 120000 15000 20 | tail -2; done
for m in 0 1 2; do KICK=900 $W/e2e $W/a.mp4 12 $m 40000 2 | tail -2; done
echo "== yuv android"; g++ -std=c++20 -O1 -Indkmock -I$S -I$S/video -Wl,--unresolved-symbols=ignore-all yuv_android_test.cpp -o $W/yuv
ffmpeg -v error -y -f lavfi -i color=c=0x3080C0:s=64x48:r=1 -frames:v 1 -vf "scale=out_color_matrix=bt709:out_range=tv,format=nv12" -f rawvideo $W/nv12.raw
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -s 64x48 -i $W/nv12.raw -vf "scale=in_color_matrix=bt709:in_range=tv:out_range=pc,format=rgba" -f rawvideo $W/ref.rgba
$W/yuv $W/nv12.raw $W/ref.rgba 64 48 1 2 0
echo "== compilacion de la UI contra el mock"; for f in $S/ui/VideoPopup.cpp $S/playlog.cpp; do g++ -std=c++23 -fsyntax-only -Dfar= -Dnear= -Imock -I$S -I$S/engine -I$S/video -Wall -Wextra -Wno-unused-parameter $f && echo "$f OK"; done
rm -rf $W; echo "TODO OK"
