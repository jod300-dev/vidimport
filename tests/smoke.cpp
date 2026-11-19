#include "engine.hpp"
#include "gdexport.hpp"
#include <cstdio>
int main(){
  gdx::Image im; im.w=128; im.h=72; im.rgba.resize(128*72*4);
  for(int y=0;y<72;++y)for(int x=0;x<128;++x){auto*p=im.at(x,y);p[0]=x*2;p[1]=y*3;p[2]=(x^y)*3;p[3]=255;}
  gdx::Options o; o.budget=300; o.maxRaster=128;
  auto r=gdx::convert(im,gdx::Method::BSP,o);
  std::printf("shapes=%zu psnr=%.2f raster=%dx%d\n",r.shapes.size(),r.psnr,r.rasterW,r.rasterH);
  gdx::GdExportOptions eo; auto s=gdx::toGdString(r,eo); std::printf("len=%zu\n%.200s\n",s.size(),s.c_str());
}
