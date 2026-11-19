#!/bin/sh
# Pruebas del motor (sin Geode): compila y ejecuta todo con g++ >= 11. Necesita ffmpeg solo para generar un MP4 de prueba.
set -e
cd "$(dirname "$0")"
INC="-I../src/engine -I../src/video -I../include"
g++ -std=c++20 -O2 -pthread $INC vtest.cpp -o vtest && ./vtest                         # patron de prueba: 7 metodos
g++ -std=c++20 -O2 -pthread $INC vcheck.cpp -o vcheck && ./vcheck                       # simulador: lo que "se ve" == frames originales
if command -v ffmpeg >/dev/null 2>&1; then
  ffmpeg -y -v error -f lavfi -i "testsrc2=size=1280x720:rate=30:duration=2" -f lavfi -i "mandelbrot=size=1280x720:rate=30" \
         -f lavfi -i "smptebars=size=1280x720:rate=30:duration=2" \
         -filter_complex "[0:v]trim=duration=2[a];[1:v]trim=duration=2,setpts=PTS-STARTPTS[b];[2:v]trim=duration=2[c];[a][b][c]concat=n=3:v=1:a=0,format=yuv420p" \
         -t 6 -c:v libx264 -crf 20 test.mp4
  g++ -std=c++20 -O2 -pthread -DVX_WITH_STB $INC vreal.cpp ../src/stb_impl.cpp -o vreal
  for fps in 12 30; do for c in 0 1 2 3 4 5 6 7; do ./vreal $fps 30000 $c 160 | grep -E "coder=|simulated"; done; done   # uso: ./vreal FPS PRESUPUESTO METODO(0-7) LADO
fi
