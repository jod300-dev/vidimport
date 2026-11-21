#include "../src/video/mp4probe.hpp"
#include <iostream>
int main(int argc,char**argv){ for(int i=1;i<argc;i++){ auto in=vid::probe(argv[i]); if(!in.ok){std::cout<<argv[i]<<": ERR "<<in.error<<"\n";continue;}
 printf("%s: %dx%d disp %dx%d rot=%d dur=%.3f fps=%.3f frames=%u codec=%s(%s) prof=%d audio=%d decodable=%d\n",argv[i],in.width,in.height,in.displayW,in.displayH,in.rotation,in.duration,in.fps,in.frames,in.codec.c_str(),in.codecName().c_str(),in.avcProfile,in.hasAudio,in.decodable()); } }
