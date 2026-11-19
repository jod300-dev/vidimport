#include "director.hpp"
#include <cstdio>
int main(){
  vx::Params P; P.originX=0; P.originY=2400;
  vx::Output o = vx::encodeTestPattern(P, 12);
  std::printf("ok=%d err=%s shapes=%zu trig=%zu groups=%d chunks=%zu psnr=%.1f\n", o.ok(), o.error.c_str(), o.shapes,o.triggers,o.groupsUsed,o.chunks.size(),o.psnr);
  for (auto&s:o.shots) std::printf(" shot %d-%d %s shapes=%d trig=%d grp=%d psnr=%.1f min=%.1f | %s\n", s.a,s.b, vx::kShotNames[int(s.coder)], s.shapes,s.triggers,s.groups,s.psnr,s.minPsnr,s.note.c_str());
}
