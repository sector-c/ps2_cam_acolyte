// Original contribution under the repository's BSD-3-Clause license.
// Game facts: US retail SLUS-20645, CRC 7290669C, revision 1.00.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tc3_camera {
constexpr uint32_t context = 0x019a4bf0;
constexpr uint32_t mirror = 0x80000000;
constexpr uint32_t constant(unsigned i) { return context + 0xec + 32*i; }
constexpr uint32_t selector(unsigned i) { return context + 0x100 + 32*i; }
constexpr uint32_t stream(unsigned i) { return context + 0xe4 + 32*i; }
using words = std::array<uint32_t, 6>;
using pose = std::array<double, 6>;
constexpr double pi = 3.14159265358979323846;
constexpr double scale = 12800.;
constexpr double turn = 65536.;
constexpr words held{1,1,1,1,1,1};
struct sample {
    std::array<uint32_t,3> refs{};
    uint32_t header{}, clock{}, cursor{}, start{}, duration{};
    words streams{}, modes{}, constants{}, output{};
};
inline bool original_modes(const words& m) {
    // Additional mission families observed on original US retail runtime.
    // These are admission candidates, not live override qualification.
    return m == words{5,5,5,5,5,0} || m == words{5,5,5,1,5,0} ||
           m == words{3,5,5,1,1,1} || m == words{5,3,5,5,5,0} ||
           m == words{3,5,5,1,1,0};
}
inline bool layout(const sample& s) {
    if (s.refs != std::array<uint32_t,3>{context,context,context} ||
        (s.header >> 16) != 1 || (s.header & 0xffff) == 0 ||
        (s.header & 0xffff) >= 32768 || !s.duration) return false;
    for (unsigned i=0;i<6;++i)
        if (s.streams[i]<16 || s.streams[i]>=0x02000000 ||
            (s.streams[i]&1) || (i && s.streams[i]<s.streams[i-1])) return false;
    return true;
}
inline bool same_track(const sample& a,const sample& b) {
    return layout(a) && layout(b) && a.header==b.header &&
           a.streams==b.streams && a.start==b.start &&
           a.duration==b.duration && uint32_t(b.clock-a.clock)<0x80000000u &&
           b.cursor>=a.cursor;
}
inline bool owned(const sample& original,const sample& now,const words& last) {
    return same_track(original,now) && now.modes==held && now.constants==last;
}
inline int32_t signed_word(uint32_t x) {
    return x<=0x7fffffffu ? static_cast<int32_t>(x) :
        static_cast<int32_t>(static_cast<int64_t>(x)-0x100000000ll);
}
inline int32_t signed_angle(uint32_t x) {
    x &= 0xffffu;
    return x<32768 ? static_cast<int32_t>(x) : static_cast<int32_t>(x)-65536;
}
inline pose decode(const words& v) {
    pose p{};
    for (unsigned i=0;i<3;++i) p[i]=signed_word(v[i])/scale;
    for (unsigned i=3;i<6;++i) p[i]=signed_angle(v[i])*2*pi/turn;
    return p;
}
inline bool encode(const pose& p,words& out) {
    for (double x:p) if (!std::isfinite(x)) return false;
    for (unsigned i=0;i<3;++i) {
        if (std::abs(p[i])>200.) return false;
        out[i]=static_cast<uint32_t>(std::llround(p[i]*scale));
    }
    for (unsigned i=3;i<6;++i)
        out[i]=static_cast<uint32_t>(std::llround(std::remainder(p[i],2*pi)*turn/(2*pi)))&0xffffu;
    return true;
}
inline void integrate(pose& p,double strafe,double forward,double up,
                      double yaw,double pitch,double roll,double dt,double speed) {
    dt=std::clamp(dt,0.,0.05);
    p[3]=std::remainder(p[3]+pitch*dt*2.,2*pi);
    p[4]=std::remainder(p[4]+yaw*dt*2.,2*pi);
    p[5]=std::remainder(p[5]+roll*dt,2*pi);
    const auto sx=std::sin(p[3]),cx=std::cos(p[3]);
    const auto sy=std::sin(p[4]),cy=std::cos(p[4]);
    const auto sz=std::sin(p[5]),cz=std::cos(p[5]);
    // Local right from Rz Rx Ry; view forward is negative local Z.
    const std::array<double,3> right{cz*cy+sz*sx*sy,sz*cx,-cz*sy+sz*sx*cy};
    const std::array<double,3> front{-cx*sy,sx,-cx*cy};
    for(unsigned i=0;i<3;++i)
        p[i]=std::clamp(p[i]+dt*speed*(strafe*right[i]+forward*front[i]+(i==1?up:0.)),-200.,200.);
}
}
