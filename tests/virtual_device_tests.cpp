#include "virtual_microphone.hpp"
#include "cable_install_finish.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    HANDLE parent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(virtual_mic::GuardState), nullptr);
    virtual_mic::GuardState* state = static_cast<virtual_mic::GuardState*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(virtual_mic::GuardState)));
    std::atomic<int> enables{0}, disables{0}, reports{0};
    std::thread worker;
    DWORD enableResult = CR_SUCCESS, disableResult = CR_SUCCESS;
    int count = 1, exitCode = -1;
    HANDLE clone(HANDLE h) {
        HANDLE copy = nullptr;
        require(DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS), "duplicate handle");
        return copy;
    }
    void start(const std::wstring& name) {
        require(parent && stop && ready && state, "create test handles");
        state->result = virtual_mic::Pending;
        HANDLE p=clone(parent), s=clone(stop), r=clone(ready), m=clone(mapping);
        worker = std::thread([this,p,s,r,m,name] {
            exitCode = virtual_mic::runGuardWith(p,s,r,m,name.c_str(),
                [this] { return std::vector<std::wstring>(count,L"TEST-ONLY-NO-REAL-DEVICE"); },
                [this](const std::wstring& id, bool enabled) -> CONFIGRET {
                    if (id != L"TEST-ONLY-NO-REAL-DEVICE") return CR_INVALID_DEVNODE;
                    if (enabled) { ++enables; return enableResult; }
                    ++disables; return disableResult;
                }, [this](DWORD) { ++reports; });
        });
        require(WaitForSingleObject(ready,3000)==WAIT_OBJECT_0,"guard startup timeout");
    }
    void finish(bool crash=false) { SetEvent(crash ? parent : stop); worker.join(); }
    ~Fixture() {
        if(worker.joinable()) { SetEvent(stop); worker.join(); }
        if(state) UnmapViewOfFile(state);
        for(HANDLE h:{parent,stop,ready,mapping}) if(h) CloseHandle(h);
    }
};
int main() {
    try {
        const std::wstring name=L"Local\\PortableMixer.Test.Lease."+std::to_wstring(GetCurrentProcessId());
        { Fixture f; f.start(name); require(f.state->result==CR_SUCCESS,"startup success");
          require(f.enables==1 && f.disables==0,"enabled only while parent lives");
          f.finish(); require(f.disables==1 && f.exitCode==0,"normal exit disables"); }
        { Fixture f; f.start(name); f.finish(true); require(f.disables==1,"parent death disables"); }
        { Fixture a; a.start(name); Fixture b; b.start(name); b.finish();
          require(b.state->result==virtual_mic::Busy && b.enables==0 && b.disables==0,"second instance must not change device");
          a.finish(); require(a.disables==1,"owner still cleans up"); }
        for(int count:{0,2}) { Fixture f; f.count=count; f.start(name); f.finish();
          require(f.enables==0 && f.disables==0,"missing or ambiguous adapter must not mutate devices"); }
        { Fixture f; f.enableResult=CR_ACCESS_DENIED; f.start(name); f.finish();
          require(f.state->result==CR_ACCESS_DENIED && f.disables==1,"failed enable cleans partial start"); }
        { Fixture f; SetEvent(f.parent); f.start(name); f.finish();
          require(f.enables==0 && f.disables==1,"parent died before enable"); }
        { Fixture f; f.disableResult=CR_REMOVE_VETOED; f.start(name); f.finish();
          require(f.disables==5 && f.reports==1 && f.exitCode==3,"disable failure retries and reports"); }
        { Fixture f; f.disableResult=CR_NO_SUCH_DEVNODE; f.start(name); f.finish();
          require(f.disables==1 && f.reports==0 && f.exitCode==0,"removed device is already disconnected"); }
        using cable_install_finish::Status;
        auto one=[] { return std::vector<std::wstring>{L"TEST"}; };
        auto ok=[](const std::wstring&) -> CONFIGRET { return CR_SUCCESS; };
        auto verified=[](const std::wstring&) { return true; };
        auto pause=[] {};
        require(cable_install_finish::settle(one,ok,verified,pause).status==Status::Disabled,"installed device disabled immediately");
        { int probes=0, writes=0;
          auto delayed=[&] { return ++probes<3 ? std::vector<std::wstring>{} : one(); };
          auto disable=[&](const std::wstring&) -> CONFIGRET { ++writes; return CR_SUCCESS; };
          require(cable_install_finish::settle(delayed,disable,verified,pause).status==Status::Disabled && probes==3 && writes==1,"delayed PnP arrival"); }
        require(cable_install_finish::settle([] { return std::vector<std::wstring>{}; },ok,verified,pause).status==Status::NotFound,"cancelled or pending install is not success");
        require(cable_install_finish::settle(one,ok,[](const std::wstring&) { return false; },pause).status==Status::Failed,"API success without disabled state is not success");
        { int writes=0;
          auto many=[] { return std::vector<std::wstring>{L"A",L"B"}; };
          auto disable=[&](const std::wstring&) -> CONFIGRET { ++writes; return CR_SUCCESS; };
          require(cable_install_finish::settle(many,disable,verified,pause).status==Status::Ambiguous && writes==0,"ambiguous install leaves devices untouched"); }
        std::cout << "5 post-install cleanup scenarios passed.\n";
        std::cout << "9 virtual-device lifecycle scenarios passed (mock device operations; real Windows waits/handles).\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
