// Prueba del muestreador: decodifica con el backend ffmpeg y compara con la referencia (ffmpeg directo) a 12 y 30 fps.
#include "../src/video/videosource.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vid;
int main(int argc, char** argv) {
    std::string path = argv[1]; double fps = atof(argv[2]), start = argc > 3 ? atof(argv[3]) : 0, len = argc > 4 ? atof(argv[4]) : 0; int store = argc > 5 ? atoi(argv[5]) : 320;
    Info in = probe(path); if (!in.ok) { printf("probe error %s\n", in.error.c_str()); return 1; }
    SampleSpec sp; sp.path = path; sp.start = start; sp.length = len; sp.fps = fps; sp.storeLong = store;
    std::atomic<float> prog{0};
    SampleResult r = sampleVideo(sp, in, &prog);
    if (!r.ok()) { printf("ERR %s\n", r.error.c_str()); return 1; }
    printf("backend=%s frames=%zu size=%dx%d (src %dx%d) len=%.2f\n", r.backend.c_str(), r.frames.size(), r.frames[0].w, r.frames[0].h, r.srcW, r.srcH, r.length);
    FILE* f = fopen("/tmp/sampled.rgba", "wb"); for (auto& im : r.frames) fwrite(im.rgba.data(), 1, im.rgba.size(), f); fclose(f);
}
