#include "../ps2_cam_acolyte/games/time_crisis_3_track.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
namespace tr=tc3_track;
std::vector<uint16_t> record(const tr::channels& modes) {
    constexpr unsigned n=18;std::array<std::vector<uint16_t>,6> parts;unsigned tag=0xfe00;
    for(unsigned i=0;i<6;++i) {
        unsigned m=modes[i];bool direct=false;
        if(i<3) {unsigned bits=1;if(m==0){bits=0;direct=true;}if(m==3){bits=2;direct=true;}if(m==4){bits=3;direct=true;}tag|=bits<<(2*i);}
        else if(m==3){tag|=1u<<(i+3);direct=true;}
        auto& p=parts[i];if(!direct)p.push_back(uint16_t(m));
        if(m==1)p.push_back(uint16_t(10+i));
        if(m==2){p.push_back(0);p.push_back(uint16_t(10+i));}
        if(m==3||m==4)for(unsigned j=0;j<n;++j){if(m==4)p.push_back(0);p.push_back(uint16_t(j+i));}
        if(m==5){p.push_back(n-1);p.push_back(uint16_t(10+i));}
    }
    std::vector<uint16_t> out{uint16_t(tag),n};for(auto& p:parts)out.push_back(uint16_t(p.size()));
    for(auto& p:parts)out.insert(out.end(),p.begin(),p.end());
    return out;
}
int main(int argc,char** argv) {
    unsigned count=0;constexpr std::array<unsigned,5> angles{0,1,2,3,5};tr::channels endpoints{};
    for(unsigned x=0;x<6;++x)for(unsigned y=0;y<6;++y)for(unsigned z=0;z<6;++z)
        for(auto p:angles)for(auto a:angles)for(auto r:angles) {
            tr::channels m{x,y,z,p,a,r};auto raw=record(m);assert(tr::parse(raw,18,m,endpoints));++count;
        }
    assert(count==27000);
    tr::channels m{5,5,5,1,1,0};auto raw=record(m);assert(tr::parse(raw,18,m,endpoints));
    auto wrong=m;wrong[0]=3;assert(!tr::parse(raw,18,wrong,endpoints));
    raw.pop_back();assert(!tr::parse(raw,18,m,endpoints));
    raw=record(m);raw[9]=0x3fff;assert(!tr::parse(raw,18,m,endpoints));
    m={1,1,1,1,1,1};raw=record(m);assert(tr::parse(raw,18,m,endpoints));
    assert(tr::native_constants(m,endpoints,endpoints));wrong=endpoints;++wrong[0];assert(!tr::native_constants(m,wrong,endpoints));
    std::cout<<count<<" synthetic mode combinations passed\n";
    if(argc==2) {
        std::ifstream in(argv[1],std::ios::binary);assert(in);unsigned pass=0,fail=0;
        uint32_t n=0;
        while(in.read(reinterpret_cast<char*>(&n),4)) {
            assert(n>=8&&n<=32768);tr::channels native{};in.read(reinterpret_cast<char*>(native.data()),24);
            std::vector<uint16_t> data(n);in.read(reinterpret_cast<char*>(data.data()),n*2);assert(in);
            if(tr::parse(data,data[1],native,endpoints))++pass;else ++fail;
        }
        assert(in.eof()&&pass==1138&&fail==3);std::cout<<pass<<" original tracks passed, "<<fail<<" refused\n";
    }
}
