#include "added_programs.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
class Volume final : public ISimpleAudioVolume {
    ULONG refs_=1;
    float level_=1;
    BOOL muted_=FALSE;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetMasterVolume(float v,LPCGUID) override { level_=v; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMasterVolume(float* v) override { *v=level_; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMute(BOOL v,LPCGUID) override { muted_=v; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMute(BOOL* v) override { *v=muted_; return S_OK; }
};
sessions::Channel channel(const wchar_t* path,DWORD pid) {
    sessions::Channel c; c.application=path; c.name=L"player.exe"; c.pid=pid; c.device=L"Speakers";
    Microsoft::WRL::ComPtr<ISimpleAudioVolume> volume; volume.Attach(new Volume()); c.volumes.push_back(volume);
    return c;
}
}
int main() {
    try {
        auto a=channel(L"C:\\Apps\\Player.exe",10), b=channel(L"c:\\apps\\PLAYER.exe",11), other=channel(L"C:\\Other\\Player.exe",12);
        std::vector<sessions::Channel> available{a,b,other};
        sessions::AddedPrograms added;
        require(added.reconcile(available).empty(),"Unselected programs must never appear initially");
        auto key=sessions::applicationKey(a);
        require(added.add(key,a.name),"Explicit add must succeed");
        require(!added.add(sessions::applicationKey(b),b.name),"Same executable must not be added twice");
        auto visible=added.reconcile(available);
        require(visible.size()==1 && visible[0].volumes.size()==2,"Multiple PIDs and devices for an app must merge into one strip");
        require(SUCCEEDED(visible[0].setLevel(.4f)),"Grouped volume change must succeed");
        a.read(); b.read(); other.read();
        require(std::abs(a.level-.4f)<.0001f && std::abs(b.level-.4f)<.0001f,"Volume must reach all selected app sessions");
        require(other.level==1,"Same filename in another directory must remain untouched");
        visible=added.reconcile({other});
        require(visible.size()==1 && visible[0].volumes.empty(),"Closed app must retain an offline strip without binding another app");
        auto restarted=channel(L"C:\\Apps\\Player.exe",99);
        require(added.reconcile({restarted})[0].pid==99,"Selected app must reconnect after restart");
        require(added.add(sessions::applicationKey(other),L"other.exe"),"Different executable path must remain selectable");
        require(added.reconcile(available)[0].id==key,"Refresh must preserve explicit addition order");
        added.remove(0);
        visible=added.reconcile(available);
        require(visible.size()==1 && visible[0].pid==12,"Removing a strip must keep other selections");
        added.remove(0); require(added.reconcile(available).empty(),"Removing all selections must restore empty state");
        mix::OutputControl bus;
        bus.gain=.72f;
        sessions::Channel master; master.master=true; master.output=&bus;
        master.read();
        require(master.online() && master.volumes.empty() && std::abs(master.level-.72f)<.0001f,"Mix output must read its independent output fader");
        master.setLevel(.53f); master.setMute(true);
        require(std::abs(bus.gain.load()-.53f)<.0001f && bus.muted.load(),"Master controls must update mix-output state");
        other.read(); require(other.level==1 && !other.muted,"Mix output must not rewrite individual app volume or mute");
        bus.active=true; bus.peak=.22f; master.read();
        require(std::abs(master.peak-.22f)<.0001f,"Meter must display actual mix bus peak");
        bus.active=false; master.read(); require(master.peak==0,"Stopped mix must show no signal");
        std::cout<<"Added-program filtering, grouping, volume isolation, restart and removal passed.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
