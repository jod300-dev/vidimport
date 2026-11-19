#include "director.hpp"
#include "vsim.hpp"
#include <cstdio>
using namespace vx;
struct Res { double avg=0, mn=99; int leak=0; std::vector<double> per; };
// cutoff: instante dentro del frame en el que se mira (s). Devuelve PSNR simulado por frame y cuantos objetos fuera del cuadro se ven en una pantalla ancha.
Res check(const Clip& c, const Output& o, bool verbose=false) {
  vsim::Sim sim; sim.load(o.chunks); sim.startPositionTriggers();
  double fw,fh; frameBox(c.w,c.h,fw,fh); double ppu=double(c.w)/fw, dt=1.0/c.fps; Res r; double sum=0;
  for (int i=0;i<c.count();++i){
    const int F=int(c.fps); const int shot=i/F; double t;
    if(shot==1) t = i%F==0 ? i*dt+0.01 : i*dt-1e-4;           // mosaico con fundido: el color llega a su valor justo al final del intervalo
    else if(shot==2) t=i*dt+0.002;                                   // mosaico+detalle: justo tras cambiar el parche
    else if(shot==5) t = i==0 ? 0.01 : i*dt+1e-4;                    // fundido: el keyframe ya esta completo en su instante
    else t=i*dt+std::min(0.045,0.8*dt);
    sim.advance(t);
    Image im=sim.render(t,fw,fh,ppu);
    if (im.w!=c.w||im.h!=c.h){ Image r2; r2.w=c.w;r2.h=c.h;r2.rgba.assign(size_t(c.w)*c.h*4,0); for(int y=0;y<std::min(im.h,c.h);++y)for(int x=0;x<std::min(im.w,c.w);++x)std::memcpy(r2.at(x,y),im.at(x,y),4); im=r2; }
    double p=psnrRGB(im,c.frames[size_t(i)]); sum+=p; r.mn=std::min(r.mn,p); r.per.push_back(p);
    // fuga: objetos visibles en una pantalla 21:9 (747x320) cuyo centro queda fuera del area del cuadro +- 5 u (excepto la zona del panorama, que se mide aparte)
    std::vector<int> vis; sim.render(t,747,320,0.05,&vis);
    double cx=sim.camPos(t,0), cy=sim.camPos(t,1); int lk=0;
    for(int k:vis){ const auto&ob=sim.objs[size_t(k)]; if(std::abs(ob.x-cx)>fw/2+6||std::abs(ob.y-cy)>fh/2+6) ++lk; }
    r.leak=std::max(r.leak,lk);
    if(verbose) std::printf("  f%02d sim=%.1f enc=%.1f leak=%d\n",i,p,o.framePsnr[size_t(i)],lk);
  }
  r.avg=sum/c.count(); return r;
}
int main(){
  vx::Params P; const char* nm[]={"Cinta","Mosaico","Mos+det","Epocas","Panor.","Fundido","Pulsos"};
  for(int fps:{12,30}){
    vx::Output o=vx::encodeTestPattern(P,fps); vx::Clip c=vx::makeTestClip(fps);
    Res r=check(c,o);
    std::printf("== fps=%d objs=%zu enc=%.1f sim avg=%.1f min=%.1f\n",fps,o.objects(),o.psnr,r.avg,r.mn);
    for(int s=0;s<7;++s){ double a=0,m=99; for(int i=0;i<fps;++i){a+=r.per[size_t(s*fps+i)]; m=std::min(m,r.per[size_t(s*fps+i)]);} double ea=0; for(int i=0;i<fps;++i) ea+=o.framePsnr[size_t(s*fps+i)];
      std::printf("   %-8s enc=%5.1f  sim=%5.1f (min %5.1f)\n",nm[s],ea/fps,a/fps,m); }
  }
}
