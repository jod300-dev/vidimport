#include "source.hpp"
#include "director.hpp"
#include "vsim.hpp"
#include <cstdio>
using namespace vx;
static double simPsnr(const Clip& c, const Output& o, double* leakFrac) {
  vsim::Sim sim; sim.load(o.chunks); sim.startPositionTriggers();
  double fw,fh; frameBox(c.w,c.h,fw,fh); double ppu=double(c.w)/fw, dt=1.0/c.fps, sum=0; int leak=0;
  std::vector<int> shotOf(size_t(c.count()),0); for(size_t s=0;s<o.shots.size();++s) for(int i=o.shots[s].a;i<o.shots[s].b;++i) shotOf[size_t(i)]=int(s);
  for(int i=0;i<c.count();++i){
    const ShotInfo& si=o.shots[size_t(shotOf[size_t(i)])]; int li=i-si.a; double t;
    if(si.coder==Coder::Mosaic||si.coder==Coder::MosaicDetail&&false) t = li==0? i*dt+0.01 : i*dt-1e-4;
    else if(si.coder==Coder::Crossfade) t = li==0? i*dt+0.01 : i*dt+1e-4;
    else if(si.coder==Coder::MosaicDetail) t=i*dt+0.002;
    else t=i*dt+std::min(0.045,0.8*dt);
    sim.advance(t);
    Image im=sim.render(t,fw,fh,ppu);
    Image r2; r2.w=c.w;r2.h=c.h;r2.rgba.assign(size_t(c.w)*c.h*4,0); for(int y=0;y<std::min(im.h,c.h);++y)for(int x=0;x<std::min(im.w,c.w);++x)std::memcpy(r2.at(x,y),im.at(x,y),4);
    sum+=psnrRGB(r2,c.frames[size_t(i)]);
    if (si.coder!=Coder::Panorama && i%7==0){ std::vector<int> vis; sim.render(t,747,320,0.05,&vis); double cx=sim.camPos(t,0),cy=sim.camPos(t,1); for(int k:vis){auto&ob=sim.objs[size_t(k)]; if(std::abs(ob.x-cx)>fw/2+6||std::abs(ob.y-cy)>fh/2+6){++leak;break;}} }
  }
  if(leakFrac)*leakFrac=leak; return sum/c.count();
}
int main(int argc,char**argv){
  int fps = argc>1? atoi(argv[1]):30; int budget= argc>2? atoi(argv[2]):60000; int coder = argc>3? atoi(argv[3]):7; int maxSide=argc>4?atoi(argv[4]):256;
  DecodeRequest rq; rq.path= getenv("VID")?getenv("VID"):"test.mp4"; rq.fps=fps; rq.maxSeconds= getenv("SECS")?atof(getenv("SECS")):6; rq.maxSide=maxSide;
  Clip c; std::string err; double t0=gdx::nowMs();
  if(!decodeVideo(rq,c,err)){ std::printf("decode fail: %s\n",err.c_str()); return 1; }
  std::printf("decoded %d frames %dx%d @%.0f fps in %.0f ms (src %dx%d %.1f fps %.1fs)\n",c.count(),c.w,c.h,c.fps,gdx::nowMs()-t0,c.srcW,c.srcH,c.srcFps,c.srcDuration);
  Params P; P.budget=budget; P.coder=Coder(coder); P.threads=1; P.originX=0;P.originY=2400;
  Output o=encode(c,P);
  if(!o.ok()){ std::printf("ENCODE ERR: %s\n",o.error.c_str()); return 1; }
  std::printf("coder=%d budget=%d -> objects=%zu (shapes %zu + triggers %zu) groups=%d psnr=%.1f min=%.1f time=%.0f ms chunks=%zu bytes=",coder,budget,o.objects(),o.shapes,o.triggers,o.groupsUsed,o.psnr,o.minPsnr,o.ms,o.chunks.size());
  size_t by=0; for(auto&s:o.chunks) by+=s.size(); std::printf("%zu\n",by);
  for(auto&s:o.shots) std::printf("  frames %3d-%3d  %-11s shapes=%6d trig=%5d grp=%4d psnr=%.1f min=%.1f | %s\n",s.a,s.b,kShotNames[int(s.coder)],s.shapes,s.triggers,s.groups,s.psnr,s.minPsnr,s.note.c_str());
  if(getenv("NOSIM")) return 0;
  double leak=0; double sp=simPsnr(c,o,&leak); std::printf("  simulated PSNR=%.1f  frames-with-neighbour-leak=%.0f\n",sp,leak);
}
