// SPDX-License-Identifier: BSD-3-Clause
// Original packed camera framing and terminal-sample validation.
#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace tc3_track {
using channels = std::array<uint32_t,6>;
using halfwords = std::span<const uint16_t>;
inline uint32_t signed16(uint16_t x) {
    return x<0x8000u?uint32_t(x):uint32_t(x)|0xffff0000u;
}
inline bool endpoint(halfwords data,unsigned mode,unsigned samples,uint32_t& value) {
    if(!samples)return false;
    switch(mode) {
    case 0: if(!data.empty())return false;value=0;return true;
    case 1: if(data.size()!=1)return false;value=signed16(data[0]);return true;
    case 2: if(data.size()!=2)return false;value=(uint32_t(data[0])<<16)|data[1];return true;
    case 3: if(data.size()!=samples)return false;value=signed16(data.back());return true;
    case 4: if(data.size()!=2u*samples)return false;value=(uint32_t(data[data.size()-2])<<16)|data.back();return true;
    case 5: {
        int last=-1;unsigned pos=0;
        while(pos<data.size()) {
            unsigned flags=data[pos++];int key=int(flags&0x3fff);
            if(key<=last||key>=int(samples))return false;
            unsigned width=(flags&0x8000)?2:1;
            if(width>data.size()-pos)return false;
            value=width==2?(uint32_t(data[pos])<<16)|data[pos+1]:signed16(data[pos]);
            pos+=width;
            if(flags&0x4000) {
                unsigned residual=unsigned(last<0?key:key-last)/2;
                if(residual>data.size()-pos)return false;
                pos+=residual;
            }
            last=key;
        }
        return last==int(samples)-1;
    }
    default:return false;
    }
}
inline bool parse(halfwords record,unsigned samples,const channels& modes,channels& values) {
    if(record.size()<8||record.size()>32768||samples==0||samples>=32768||record[1]!=samples)return false;
    const unsigned tag=record[0];if((tag&0xfe00)!=0xfe00)return false;
    unsigned pos=8;
    for(unsigned i=0;i<6;++i) {
        const unsigned len=record[2+i];if(len>record.size()-pos)return false;
        auto payload=record.subspan(pos,len);pos+=len;
        int mode=-1;
        if(tag==0xffff)mode=i<3?4:3;
        else if(i<3)mode=std::array<int,4>{0,-1,3,4}[(tag>>(2*i))&3];
        else if((tag>>(i+3))&1)mode=3;
        if(mode<0) {
            if(payload.empty())return false;
            mode=payload.front();payload=payload.subspan(1);
            if(mode!=0&&mode!=1&&mode!=2&&mode!=5)return false;
        }
        if(unsigned(mode)!=modes[i]||!endpoint(payload,unsigned(mode),samples,values[i]))return false;
        if(i>=3)values[i]&=0xffffu;
    }
    return pos==record.size();
}
inline bool native_constants(const channels& modes,const channels& constants,const channels& endpoints) {
    for(unsigned i=0;i<6;++i)
        if((modes[i]==1||modes[i]==2)&&constants[i]!=endpoints[i])return false;
    return true;
}
}
