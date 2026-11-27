// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <array>
#include <cmath>
#include <algorithm>
namespace tc3_producer {
using pose=std::array<double,6>;
using matrix=std::array<double,16>;
constexpr double pi=3.14159265358979323846;
inline matrix compose(const pose& p) {
    const auto sx=std::sin(p[3]),cx=std::cos(p[3]),sy=std::sin(p[4]),cy=std::cos(p[4]),sz=std::sin(p[5]),cz=std::cos(p[5]);
    return {cz*cy+sz*sx*sy,sz*cx,-cz*sy+sz*sx*cy,0,-sz*cy+cz*sx*sy,cz*cx,sz*sy+cz*sx*cy,0,cx*sy,-sx,cx*cy,0,p[0],p[1],p[2],1};
}
inline pose decompose(const matrix& m) {
    const double pitch=std::asin(std::clamp(-m[9],-1.,1.));
    double yaw=std::atan2(m[8],m[10]),roll=std::atan2(m[1],m[5]);
    if(std::abs(std::cos(pitch))<1e-7){roll=0;yaw=std::atan2(-m[2],m[0]);}
    return {m[12],m[13],m[14],pitch,yaw,roll};
}
inline bool near(const matrix& a,const matrix& b) {
    for(unsigned i=0;i<16;++i)if(!std::isfinite(a[i])||!std::isfinite(b[i])||std::abs(a[i]-b[i])>(i>=12&&i<=14?.025:.003))return false;
    return true;
}
inline bool rigid(const matrix& m) {
    for(double x:m)if(!std::isfinite(x))return false;
    for(unsigned i=0;i<3;++i) {
        if(std::abs(m[4*i+3])>.002||std::abs(m[12+i])>10000)return false;
        for(unsigned j=0;j<3;++j) {
            double d=0;for(unsigned k=0;k<3;++k)d+=m[4*i+k]*m[4*j+k];
            if(std::abs(d-(i==j?1.:0.))>.003)return false;
        }
    }
    double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return std::abs(det-1)<.004&&std::abs(m[15]-1)<.002;
}
inline void integrate(pose& p,double side,double front,double up,double yaw,double pitch,double roll,double dt,double speed) {
    dt=std::clamp(dt,0.,.05);
    p[3]=std::remainder(p[3]+pitch*dt*2,2*pi);p[4]=std::remainder(p[4]+yaw*dt*2,2*pi);p[5]=std::remainder(p[5]+roll*dt,2*pi);
    auto m=compose(p);
    for(unsigned i=0;i<3;++i)p[i]=std::clamp(p[i]+dt*speed*(side*m[i]-front*m[8+i]+(i==1?up:0.)),-10000.,10000.);
}
}
