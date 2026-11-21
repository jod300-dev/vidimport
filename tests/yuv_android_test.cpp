#define __ANDROID__ 1
#include "../src/video/decode_android.cpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace vid;
int main(int argc, char** argv) {
    // argv[1] = nv12 raw (w*h*3/2), argv[2] = rgba raw de referencia, w h, standard (1 = BT.709, 2 = BT.601), range (1 full, 2 limited), i420 flag
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> yuv((std::istreambuf_iterator<char>(f)), {});
    std::ifstream g(argv[2], std::ios::binary); std::vector<uint8_t> ref((std::istreambuf_iterator<char>(g)), {});
    OutFmt o; o.w = atoi(argv[3]); o.h = atoi(argv[4]); o.stride = o.w; o.slice = o.h; o.standard = atoi(argv[5]); o.range = atoi(argv[6]);
    o.color = atoi(argv[7]) ? 19 : 21; o.cropL = 0; o.cropT = 0; o.cropR = o.w - 1; o.cropB = o.h - 1;
    std::vector<uint8_t> rgba; int w, h;
    if (!yuvToRgba(yuv.data(), yuv.size(), o, rgba, w, h)) { printf("conversion failed\n"); return 1; }
    double err = 0; int maxd = 0; for (size_t i = 0; i < rgba.size(); i += 4) for (int c = 0; c < 3; ++c) { int d = std::abs(int(rgba[i+c]) - int(ref[i+c])); err += d*d; maxd = std::max(maxd, d); }
    double mse = err / (rgba.size() / 4 * 3); printf("std=%s range=%s %s: mse %.2f maxdiff %d psnr %.1f\n", o.standard==1?"709":"601", o.range==1?"full":"limited", argv[7][0]=='1'?"I420":"NV12", mse, maxd, 10*log10(255.0*255.0/std::max(mse,1e-9)));
}
