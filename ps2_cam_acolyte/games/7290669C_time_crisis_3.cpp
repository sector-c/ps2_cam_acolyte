// Original contribution under the repository's BSD-3-Clause license.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../ps2.h"
#include "imgui.h"
#include "time_crisis_3_producer.h"
#include <array>
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <cstdio>
#include <stdexcept>
#include <memory>
#include <vector>
#include <string>
#pragma comment(lib, "bcrypt.lib")

namespace {
namespace tc = tc3_producer;
using ipc_type = PINE::PCSX2;
struct batch {
    PINE::Shared::BatchCommand value;
    explicit batch(ipc_type& ipc):value(ipc.FinalizeBatch()){}
    batch(const batch&)=delete;
    ~batch(){value.Free();}
};
void require(bool good,const char* why) { if(!good) throw std::runtime_error(why); }

constexpr uint32_t ctx=0x019a4bf0, site=0x180260, native_call=0x0c043ecc, local_addr=ctx+0xc50;
struct sample {
    std::array<uint32_t,3> refs{};
    uint32_t code{},alternate{},pause{},simulation{},clock{};
    tc::matrix local{},global{},render{};
    bool primary() const {return refs==std::array<uint32_t,3>{ctx,ctx,ctx}&&alternate==native_call;}
    bool published(const tc::matrix& m) const {return tc::near(local,m)&&tc::near(global,m)&&tc::near(render,m);}
};
class transport {
    ipc_type& ipc;
    static uint32_t addr(uint32_t a){return 0x80000000u+a;}
    uint32_t word(const batch& b,int i){return ipc.GetReply<PINE::Shared::MsgRead32>(b.value,i);}
public:
    explicit transport(ipc_type& c):ipc(c){}
    void identity(bool full=false) {
        std::unique_ptr<char[]> uuid(ipc.GetGameUUID());
        require(uuid&&_stricmp(uuid.get(),"7290669C")==0,"Not supported TC3 executable");
        if(full) {
            std::unique_ptr<char[]> serial(ipc.GetGameID()),rev(ipc.GetGameVersion());
            require(serial&&rev&&std::strcmp(serial.get(),"SLUS-20645")==0&&std::strcmp(rev.get(),"1.00")==0,"Unsupported disc revision");
        }
    }
    bool running(){return ipc.Status()==PINE::Shared::Running;}
    sample read() {
        ipc.InitializeBatch();
        for(auto a:{ctx-8,ctx-4,0x019a665cu,site,0x180190u,0x43ab80u,0x43ab84u,0x43ab88u})ipc.Read<uint32_t,true>(addr(a));
        for(auto a:{local_addr,0x441a50u,0x1ea9980u})for(unsigned i=0;i<16;++i)ipc.Read<uint32_t,true>(addr(a+4*i));
        batch b(ipc);ipc.SendCommand(b.value);sample s;
        for(int i=0;i<3;++i)s.refs[i]=word(b,i);
        s.code=word(b,3);s.alternate=word(b,4);s.pause=word(b,5);s.simulation=word(b,6);s.clock=word(b,7);
        for(unsigned i=0;i<16;++i) {
            auto convert=[&](int j){uint32_t w=word(b,j);float f;std::memcpy(&f,&w,4);return double(f);};
            s.local[i]=convert(8+i);s.global[i]=convert(24+i);s.render[i]=convert(40+i);
        }
        return s;
    }
    std::vector<unsigned char> read_span(uint32_t a,unsigned bytes) {
        std::vector<unsigned char> data(bytes);ipc.InitializeBatch();
        for(unsigned i=0;i<bytes;i+=4)ipc.Read<uint32_t,true>(addr(a+i));
        batch b(ipc);ipc.SendCommand(b.value);
        for(unsigned i=0;i<bytes;i+=4) {
            uint32_t w=word(b,i/4);for(unsigned k=0;k<4;++k)data[i+k]=static_cast<unsigned char>(w>>(8*k));
        }
        return data;
    }
    void fingerprint(uint32_t a,unsigned n,const char* expected,bool allow_patch=false) {
        auto data=read_span(a,n);
        if(a==0x180240&&allow_patch&&data[32]==0&&data[33]==0&&data[34]==0&&data[35]==0) {
            for(unsigned i=0;i<4;++i)data[32+i]=static_cast<unsigned char>(native_call>>(8*i));
        }
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
    void verify_code(bool allow_patch=false) {
        fingerprint(0x180240,136,"084b5ffefb6790a8ac44da16dcdfc8d2e5bc07de7d7b7f2f102023fbd27dd4f2",allow_patch);
        fingerprint(0x180170,128,"0551ad7fecf2293892e907210f58c6d481264a819be57018c86beda8f47bcdd1");
        fingerprint(0x10fb30,192,"f718448ffa494fc2c04b889c831e7f4ceccc742624e13d37b136225ccc600fa2");
        fingerprint(0x10fbf0,192,"50dbf8388e395e3316b0b11ab8c7914e3eb2b74b4a8d71ebf036938c0ee56d9a");
    }
    void patch(uint32_t v) {
        require(v==0||v==native_call,"Invalid hook word");ipc.InitializeBatch();
        ipc.Write<uint32_t,true>(addr(site),v);batch b(ipc);ipc.SendCommand(b.value);
    }
    void write(const tc::matrix& m) {
        require(tc::rigid(m),"Invalid camera matrix");ipc.InitializeBatch();
        for(unsigned i=0;i<16;++i){float f=static_cast<float>(m[i]);uint32_t w;std::memcpy(&w,&f,4);ipc.Write<uint32_t,true>(addr(local_addr+4*i),w);}
        batch b(ipc);ipc.SendCommand(b.value);
    }
};
class time_crisis_3 final:public ps2_game {
    transport io;
    tc::pose target{};
    tc::matrix desired{};
    bool active=false,uncertain=false,toggle=false,dirty=true,recover=false;
    bool publication_pending=false,first_motion=true;
    uint32_t publication_clock=0;
    std::chrono::steady_clock::time_point deadline{};
    float speed=4.f;
    int nudge=0;
    std::string status="Ready. Enable in single-player gameplay, including native freeze.";
    void report(const char* s){status=s;dirty=true;}
    void disable() {
        if(!active&&!uncertain)return;
        io.identity(true);io.verify_code(true);
        auto s=io.read();
        if(s.code==0)io.patch(native_call);
        require(io.read().code==native_call,"Original instruction restoration failed");
        active=uncertain=publication_pending=false;report("Native matrix producer restored.");
    }
    void begin_verify(uint32_t clock) {
        publication_pending=true;publication_clock=clock;deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(300);
    }
    void enable() {
        require(!uncertain,"Uncertain hook; recover or restart game");
        io.identity(true);io.verify_code();require(io.running(),"Resume emulator itself; native freeze is allowed");
        const auto s=io.read();require(s.primary()&&s.code==native_call&&tc::rigid(s.local),"No valid primary render context");
        desired=s.local;target=tc::decompose(desired);require(tc::near(tc::compose(target),desired),"Unsupported matrix convention");
        uncertain=true;io.patch(0);io.write(desired);active=true;uncertain=false;first_motion=true;begin_verify(s.clock);
        report("Producer hook installed; verifying native render publication.");
    }
public:
    explicit time_crisis_3(const pcsx2& ps2):io(*ps2.get_ipc()){}
    void shutdown() noexcept override {try{disable();}catch(...){}}
    bool consume_ui_dirty() override {bool d=dirty;dirty=false;return d;}
    void draw_game_ui(const pcsx2&,const controller& c,playback& replay) override {
        ImGui::TextUnformatted(status.c_str());
        ImGui::TextWrapped("Experimental single-player producer hook. No track or parent dependence. Camera affects native aiming; do not shoot while enabled. Disable before multiplayer or save-state loads.");
        if(ImGui::Button(active||uncertain?"Disable / restore":"Enable Freecam"))toggle=true;
        if(!active&&!uncertain&&ImGui::Button("Recover abandoned P1 hook"))recover=true;
        ImGui::Text("Controller: %s toggles freecam",c.get_button_display_name(controller_bindings::freecam));
        ImGui::SliderFloat("Speed",&speed,.25f,20.f);
        if(active) {
            if(ImGui::Button("Left"))nudge=-1;ImGui::SameLine();if(ImGui::Button("Right"))nudge=1;
            ImGui::Text("XYZ %.3f %.3f %.3f",target[0],target[1],target[2]);
        }
        replay.draw_playback_ui(c);
    }
    void update(const pcsx2&,const controller_state& c,playback& replay,float dt) override {
        try {
            if(recover) {recover=false;io.identity(true);io.verify_code(true);require(io.read().code==0,"No abandoned producer hook");uncertain=true;disable();return;}
            if(toggle||c.button_down(controller_bindings::freecam)) {toggle=false;if(active||uncertain)disable();else enable();return;}
            if(!active)return;
            io.identity();
            if(!io.running())return;
            auto s=io.read();
            if(!s.primary()||s.code!=0) {disable();report("Render route changed; native producer restored.");return;}
            if(publication_pending) {
                if(uint32_t(s.clock-publication_clock)>=2&&uint32_t(s.clock-publication_clock)<0x80000000u&&s.published(desired)) {
                    publication_pending=false;report("Freecam active.");
                } else if(std::chrono::steady_clock::now()>deadline) {disable();report("Render publication failed; native producer restored.");}
                return;
            }
            auto l=c.get_left_axis(),r=c.get_right_axis();
            const double roll=(c.button(controller_bindings::r1)?1.:0.)-(c.button(controller_bindings::l1)?1.:0.);
            tc::integrate(target,l.first,-l.second,c.get_right_trigger()-c.get_left_trigger(),-r.first,-r.second,roll,dt,speed);
            if(nudge) {tc::integrate(target,nudge,0,0,0,0,0,.05,10.);nudge=0;}
            float yaw=static_cast<float>(target[4]),pitch=static_cast<float>(target[3]),x=static_cast<float>(target[0]),y=static_cast<float>(target[1]),z=static_cast<float>(target[2]);
            if(replay.update(dt,yaw,pitch,x,y,z)){target[0]=x;target[1]=y;target[2]=z;target[3]=pitch;target[4]=yaw;}
            auto m=tc::compose(target);
            if(m!=desired) {
                io.write(m);desired=m;
                // First changed matrix proves producer bypass, then continuous input without per-command waiting.
                if(first_motion){first_motion=false;begin_verify(s.clock);}
            } else if(!tc::near(s.local,desired)) {
                disable();report("Native matrix writer replaced override; hook restored.");
            }
        } catch(const std::exception& e) {
            if(active||uncertain)try{disable();}catch(...){active=false;uncertain=true;report("Restore uncertain; restart game.");return;}
            report(e.what());
        } catch(...) {if(active||uncertain)try{disable();}catch(...){active=false;uncertain=true;}report("PINE communication failed; restart game if restoration uncertain.");}
    }
};
ps2_game_static_register<time_crisis_3> registration("7290669C","Time Crisis 3 (USA) [Producer hook P1]");
}
