#include "../ps2_cam_acolyte/games/time_crisis_3_camera.h"
#include <cassert>
#include <limits>
#include <iostream>
namespace tc=tc3_camera;
int main(){
    tc::sample s;
    s.refs={tc::context,tc::context,tc::context};
    s.header=0x10012;s.duration=90;s.clock=100;s.cursor=4;
    s.streams={0x13268b2,0x13268dc,0x13268fa,0x132691e,0x132693c,0x132695a};
    s.modes={5,5,5,5,5,0};
    assert(tc::layout(s)&&tc::original_modes(s.modes));
    for(const auto& m: std::array<tc::words,5>{{{5,5,5,5,5,0},{5,5,5,1,5,0},{3,5,5,1,1,1},{5,3,5,5,5,0},{3,5,5,1,1,0}}}) {
        assert(tc::original_modes(m));
        auto b=s;b.modes=m;assert(tc::layout(b));
        b.modes=tc::held;assert(tc::owned(s,b,tc::words{}));
    }
    assert(!tc::original_modes(tc::held));
    assert(!tc::original_modes(tc::words{}));
    assert(!tc::layout(tc::sample{}));
    assert(!tc::original_modes(tc::words{3,3,3,1,1,1}));
    tc::pose p{-69.,2.,18.,0.,0.,0.};
    tc::words w{};
    assert(tc::encode(p,w));
    assert(tc::decode(w)==p);
    auto a=s;a.clock++;a.cursor++;a.modes=tc::held;a.constants=w;
    assert(tc::owned(s,a,w));
    a.streams[1]+=2;assert(!tc::owned(s,a,w));
    a=s;a.modes=tc::held;a.constants=w;a.clock--;assert(!tc::owned(s,a,w));
    a=s;a.modes=tc::held;a.constants=w;a.constants[0]++;assert(!tc::owned(s,a,w));
    a=s;a.modes=tc::held;a.constants=w;a.cursor--;assert(tc::owned(s,a,w)); // ordinary cover reversal
    a=s;a.refs[0]=0;assert(!tc::layout(a));
    assert(tc::pose_matches(w,w));
    auto changed=w;changed[0]+=12800;assert(!tc::pose_matches(w,changed));
    tc::sample matrices{};matrices.world=matrices.local={1,0,0,0,0,1,0,0,0,0,1,0,-69,2,18,1};
    assert(tc::matrix_matches(matrices,p));matrices.world[12]=1.877;assert(!tc::matrix_matches(matrices,p));
    tc::pose q{};tc::integrate(q,1,1,1,0,0,0,.05,2);
    assert(std::abs(q[0]-.1)<1e-9&&std::abs(q[1]-.1)<1e-9&&std::abs(q[2]+.1)<1e-9);
    tc::pose upside{0,0,0,2.,0,0};tc::integrate(upside,0,0,0,0,0,0,.05,2);
    assert(upside[3]==2.); // idle input never snaps an existing steep pitch
    p[0]=201;assert(!tc::encode(p,w));
    p[0]=std::numeric_limits<double>::quiet_NaN();assert(!tc::encode(p,w));
    p={-200,200,0,0,tc::pi,0};assert(tc::encode(p,w));assert(w[4]==32768);
    std::cout<<"TC3 camera model checks passed\n";
}
