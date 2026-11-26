// Original contribution under the repository's BSD-3-Clause license.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../ps2.h"
#include "imgui.h"
#include "time_crisis_3_camera.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <vector>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace {
namespace tc = tc3_camera;
using ipc_type = PINE::PCSX2;
struct batch {
    PINE::Shared::BatchCommand value;
    explicit batch(ipc_type& ipc):value(ipc.FinalizeBatch()){}
    batch(const batch&)=delete;
    ~batch(){value.Free();}
};
void require(bool good,const char* why) { if(!good) throw std::runtime_error(why); }

class tc3_transport {
    ipc_type& ipc;
    static uint32_t addr(uint32_t a){return tc::mirror+a;}
    uint32_t word(const batch& b,int i) {
        return ipc.GetReply<PINE::Shared::MsgRead32>(b.value,i);
    }
    uint16_t half(const batch& b,int i) {
        return ipc.GetReply<PINE::Shared::MsgRead16>(b.value,i);
    }
public:
    explicit tc3_transport(ipc_type& client):ipc(client){}
    void identity() {
        std::unique_ptr<char[]> uuid(ipc.GetGameUUID());
        require(uuid && _stricmp(uuid.get(),"7290669C")==0,"Not the supported TC3 executable");
    }
    void verify_revision() {
        identity();
        std::unique_ptr<char[]> serial(ipc.GetGameID()), version(ipc.GetGameVersion());
        require(serial && std::strcmp(serial.get(),"SLUS-20645")==0 &&
                version && std::strcmp(version.get(),"1.00")==0,"Unsupported TC3 disc revision");
    }
    bool running() { return ipc.Status()==PINE::Shared::Running; }
    tc::sample read() {
        ipc.InitializeBatch();
        for(auto a:{tc::context-8,tc::context-4,0x019a665cu})
            ipc.Read<uint32_t,true>(addr(a));
        for(auto a:{tc::context+0xe0,0x0043ab88u,tc::context+0x6c,
                    tc::context+0x74,tc::context+0x7c})
            ipc.Read<uint32_t,true>(addr(a));
        for(unsigned i=0;i<6;++i) {
            ipc.Read<uint32_t,true>(addr(tc::stream(i)));
            ipc.Read<uint32_t,true>(addr(tc::selector(i)));
            if(i<3)ipc.Read<uint32_t,true>(addr(tc::constant(i)));
            else ipc.Read<uint16_t,true>(addr(tc::constant(i)));
            if(i<3)ipc.Read<uint32_t,true>(addr(tc::context+4*i));
            else ipc.Read<uint16_t,true>(addr(tc::context+12+2*(i-3)));
        }
        batch b(ipc);ipc.SendCommand(b.value);
        tc::sample s;
        for(int i=0;i<3;++i)s.refs[i]=word(b,i);
        s.header=word(b,3);s.clock=word(b,4);s.cursor=word(b,5);
        s.start=word(b,6);s.duration=word(b,7);
        for(unsigned i=0;i<6;++i) {
            int k=8+4*i;
            s.streams[i]=word(b,k);s.modes[i]=word(b,k+1);
            s.constants[i]=i<3?word(b,k+2):half(b,k+2);
            s.output[i]=i<3?word(b,k+3):half(b,k+3);
        }
        return s;
    }
    std::vector<unsigned char> read_span(uint32_t a,unsigned bytes) {
        require(bytes%4==0,"Unaligned code span");
        std::vector<unsigned char> data(bytes);
        // A few bounded batches at activation only, not a complete RAM dump.
        for(unsigned off=0;off<bytes;off+=512) {
            unsigned n=std::min(512u,bytes-off);
            ipc.InitializeBatch();
            for(unsigned j=0;j<n;j+=4)ipc.Read<uint32_t,true>(addr(a+off+j));
            batch b(ipc);ipc.SendCommand(b.value);
            for(unsigned j=0;j<n;j+=4) {
                uint32_t w=word(b,j/4);
                for(unsigned k=0;k<4;++k)data[off+j+k]=static_cast<unsigned char>(w>>(8*k));
            }
        }
        return data;
    }
    void fingerprint(uint32_t a,unsigned n,const char* expected) {
        auto data=read_span(a,n);
        std::array<unsigned char,32> hash{};
        BCRYPT_ALG_HANDLE alg=nullptr;
        require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,
                "Cannot initialize SHA-256");
        NTSTATUS rc=BCryptHash(alg,nullptr,0,data.data(),static_cast<ULONG>(data.size()),
                               hash.data(),static_cast<ULONG>(hash.size()));
        BCryptCloseAlgorithmProvider(alg,0);
        require(rc==0,"Cannot hash camera code");
        constexpr char digits[]="0123456789abcdef";
        std::string hex;
        for(auto v:hash){hex+=digits[v>>4];hex+=digits[v&15];}
        require(hex==expected,"TC3 camera code fingerprint mismatch");
    }
    void verify_code() {
        fingerprint(0x10fb30,160,"3fd3291d94c4678d91ab5a3cf7a8968c228d2f2c2ad53db6af4aed4667acc545");
        fingerprint(0x162cb0,2280,"c0aa9ccbe5a01fe29e93922e365c975a8679adf849efb7f1294a20f623357d6b");
    }
    bool packed_layout(const tc::sample& s) {
        if(!tc::layout(s))return false;
        // Packed headers can be only halfword-aligned, not word-aligned.
        ipc.InitializeBatch();
        for(unsigned i=0;i<8;++i)ipc.Read<uint16_t,true>(addr(s.streams[0]-16+2*i));
        batch b(ipc);ipc.SendCommand(b.value);
        auto u16=[&](unsigned i){return unsigned(half(b,i/2));};
        if((u16(0)&0xfe00)!=0xfe00 || u16(2)!=(s.header&0xffff))return false;
        uint64_t next=s.streams[0];
        for(unsigned i=0;i<6;++i) {
            if(next!=s.streams[i])return false;
            next+=2*u16(4+2*i);
        }
        return next<=0x02000000;
    }
    // No stock PINE transaction is atomic relative to guest execution.
    // On a failed/partial acknowledgement callers stop, never blindly retry.
    void write(const tc::words& values,const tc::words* modes) {
        ipc.InitializeBatch();
        for(unsigned i=0;i<6;++i) {
            if(i<3)ipc.Write<uint32_t,true>(addr(tc::constant(i)),values[i]);
            else ipc.Write<uint16_t,true>(addr(tc::constant(i)),static_cast<uint16_t>(values[i]));
        }
        if(modes)for(unsigned i=0;i<6;++i)
            ipc.Write<uint32_t,true>(addr(tc::selector(i)),(*modes)[i]);
        batch b(ipc);ipc.SendCommand(b.value);
    }
};

class time_crisis_3 final : public ps2_game {
    tc3_transport io;
    tc::sample original{}, previous{};
    tc::words last{};
    tc::pose target{};
    bool enabled=false, fault=false, toggle_requested=false, dirty=true;
    float speed=4.f;
    int nudge=0;
    double activation_ms=0;
    unsigned writes=0;
    bool paused_notice=false;
    std::string status="Ready. Start gameplay, then press A or Enable.";
    void report(const char* s){status=s;dirty=true;}
    void enable() {
        const auto begin=std::chrono::steady_clock::now();
        io.verify_revision();
        require(io.running(),"Resume PCSX2 first (do not use emulator pause).");
        io.verify_code();
        const auto a=io.read();
        require(tc::original_modes(a.modes)&&io.packed_layout(a),
                "No qualified primary camera track in this scene");
        const auto b=io.read();
        require(tc::same_track(a,b)&&a.modes==b.modes,
                "Camera changed during activation; retry in a stable scene");
        target=tc::decode(b.output);
        tc::words initial{};
        require(tc::encode(target,initial),"Camera pose outside qualified bounds");
        original=previous=b;
        // Treat failed acknowledgements as possibly applied.
        fault=true;
        io.identity();
        require(io.running(),"PCSX2 paused during activation");
        io.write(initial,&tc::held);
        const auto after=io.read();
        require(tc::owned(original,after,initial),"Camera ownership could not be verified; restart game");
        last=initial;previous=after;enabled=true;fault=false;
        activation_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        writes=0;paused_notice=false;
        report("Freecam active (experimental gameplay camera).");
        std::printf("TC3 activation: %.1f ms\n",activation_ms);
    }
    void disable() {
        if(!enabled)return;
        io.identity();
        const auto now=io.read();
        if(!tc::owned(previous,now,last)) {
            enabled=false;
            fault=tc::same_track(previous,now)||!tc::original_modes(now.modes);
            report(fault?"Camera ownership lost; restart the game before retrying.":
                         "Native camera replaced the track; no stale writes made.");
            return;
        }
        fault=true;enabled=false;
        io.write(original.constants,&original.modes);
        const auto after=io.read();
        require(tc::same_track(now,after)&&after.modes==original.modes&&after.constants==original.constants,
                "Camera restoration not confirmed; restart game");
        fault=false;report("Original camera modes restored.");
    }
public:
    explicit time_crisis_3(const pcsx2& ps2):io(*ps2.get_ipc()){}
    void shutdown() noexcept override {
        if(enabled&&!fault)try{disable();}catch(...){}
    }
    bool consume_ui_dirty() override {bool d=dirty;dirty=false;return d;}
    void draw_game_ui(const pcsx2&,const controller& c,playback& replay) override {
        ImGui::TextUnformatted(status.c_str());
        ImGui::TextWrapped("Experimental US retail support. Keep PCSX2 running. Camera affects gameplay aim; do not shoot while exploring.");
        if(!fault&&ImGui::Button(enabled?"Disable Freecam":"Enable Freecam"))toggle_requested=true;
        ImGui::Text("Controller: %s toggles freecam",c.get_button_display_name(controller_bindings::freecam));
        ImGui::TextUnformatted("Left stick: move | Right stick: look | Triggers: elevation | LB/RB: roll");
        ImGui::SliderFloat("Speed",&speed,.25f,20.f);
        if(enabled) {
            ImGui::Text("Activation: %.1f ms | movement writes: %u",activation_ms,writes);
            ImGui::TextUnformatted("Test movement without a controller:");
            if(ImGui::Button("Forward"))nudge=1; ImGui::SameLine();
            if(ImGui::Button("Back"))nudge=2; ImGui::SameLine();
            if(ImGui::Button("Left"))nudge=3; ImGui::SameLine();
            if(ImGui::Button("Right"))nudge=4;
            if(ImGui::Button("Up"))nudge=5; ImGui::SameLine();
            if(ImGui::Button("Down"))nudge=6; ImGui::SameLine();
            if(ImGui::Button("Yaw +10"))nudge=7; ImGui::SameLine();
            if(ImGui::Button("Yaw -10"))nudge=8;
        }
        replay.draw_playback_ui(c);
        if(fault)ImGui::TextWrapped("Restart the game to clear an uncertain override. No automatic writes will be attempted.");
    }
    void update(const pcsx2&,const controller_state& c,playback& replay,float dt) override {
        try {
            bool toggle=toggle_requested||c.button_down(controller_bindings::freecam);
            toggle_requested=false;
            if(fault)return;
            if(toggle){if(enabled)disable();else enable();return;}
            if(!enabled)return;
            io.identity();
            if(!io.running()) {
                nudge=0;
                if(!paused_notice)report("Emulator paused. Resume PCSX2 to see camera movement.");
                paused_notice=true;return;
            }
            if(paused_notice){paused_notice=false;report("Freecam active (experimental gameplay camera).");}
            const auto now=io.read();
            if(!tc::owned(previous,now,last)) {
                enabled=false;fault=tc::same_track(previous,now)||!tc::original_modes(now.modes);
                report(fault?"Camera ownership lost; restart game.":
                             "Scene changed; enable again in the new scene.");
                return;
            }
            previous=now;
            auto l=c.get_left_axis(),r=c.get_right_axis();
            const double roll=(c.button(controller_bindings::r1)?1.:0.)-
                              (c.button(controller_bindings::l1)?1.:0.);
            tc::integrate(target,l.first,-l.second,c.get_right_trigger()-c.get_left_trigger(),
                          -r.first,-r.second,roll,dt,speed);
            if(nudge>=1&&nudge<=6) {
                const double side=nudge==3?-1.:nudge==4?1.:0.;
                const double front=nudge==1?1.:nudge==2?-1.:0.;
                const double up=nudge==5?1.:nudge==6?-1.:0.;
                tc::integrate(target,side,front,up,0,0,0,.05,10.);
            } else if(nudge==7||nudge==8) {
                target[4]=std::remainder(target[4]+(nudge==7?1.:-1.)*tc::pi/18,2*tc::pi);
            }
            nudge=0;
            float yaw=static_cast<float>(target[4]),pitch=static_cast<float>(target[3]);
            float x=static_cast<float>(target[0]),y=static_cast<float>(target[1]),z=static_cast<float>(target[2]);
            if(replay.update(dt,yaw,pitch,x,y,z)) {
                target[0]=x;target[1]=y;target[2]=z;target[3]=pitch;target[4]=yaw;
            }
            tc::words next{};
            require(tc::encode(target,next),"Camera pose outside qualified bounds");
            if(next!=last) {
                // Only six constants (18 bytes) in normal motion; never reassert selectors.
                io.write(next,nullptr);
                last=next;
                ++writes;
            }
        } catch(const std::exception& e) {
            fault=fault||enabled;enabled=false;report(e.what());
        } catch(...) {
            fault=fault||enabled;enabled=false;report("PINE communication failed; restart game if override was active.");
        }
    }
};
ps2_game_static_register<time_crisis_3> registration("7290669C","Time Crisis 3 (USA) [Experimental]");
}
