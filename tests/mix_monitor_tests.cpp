#include "mix_monitor.hpp"
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = GetTickCount64()+3000;
    do { if (predicate()) return true; Sleep(5); } while (GetTickCount64() < deadline);
    return false;
}
int main(int argc, char**) {
    try {
        MixMonitor monitor;
        mix::Block block{};
        require(!monitor.enabled.load() && !monitor.active.load(), "Speaker monitoring starts disabled");
        monitor.enable(true);
        require(monitor.statusText().find(L"믹스를 시작") != std::wstring::npos, "Idle monitoring waits for the actual mix");
        monitor.enable(false);
        monitor.start(L"virtual-output");
        for (int i=0; i<100; ++i) monitor.push(block);
        require(!monitor.active.load() && monitor.dropped.load()==0, "Disabled monitoring never queues or opens speakers");
        monitor.stop(); monitor.stop();
        monitor.start(L"virtual-output"); monitor.stop();
        if (argc > 1) {
            using Microsoft::WRL::ComPtr;
            require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize test COM");
            struct Cleanup { ~Cleanup() { CoUninitialize(); } } cleanup;
            ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device;
            require(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator))), "Create enumerator");
            require(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)), "Find speakers");
            LPWSTR raw = nullptr; require(SUCCEEDED(device->GetId(&raw)), "Read default endpoint");
            std::wstring id(raw); CoTaskMemFree(raw);
            monitor.enable(true); monitor.start(id);
            require(waitFor([&]{ return monitor.statusText().find(L"실제 스피커로 설정") != std::wstring::npos; }),
                "Using the virtual microphone as speakers must be rejected");
            require(!monitor.active.load(), "Rejected monitor route stays inactive");
            monitor.stop();
            // Real output stream fed directly by final-mix blocks. Silence avoids
            // playing an unwanted test tone or recording any other app's audio.
            monitor.start(L"different-virtual-endpoint");
            require(waitFor([&]{return monitor.active.load();}), "Default speaker render stream must start");
            for (int i=0; i<30; ++i) { monitor.push(block); Sleep(10); }
            require(monitor.active.load(), "Direct block playback must stay active");
            monitor.enable(false);
            require(waitFor([&]{return !monitor.active.load();}), "Disable closes speakers while the mix worker stays running");
            monitor.enable(true);
            require(waitFor([&]{return monitor.active.load();}), "Monitoring can be re-enabled without restarting the mix");
            monitor.push(block);
            const auto stopTime=GetTickCount64(); monitor.stop();
            require(!monitor.active.load() && GetTickCount64()-stopTime<1000, "Shutdown joins promptly");
            std::cout << "PASS: actual default-speaker playback, toggle, route guard and shutdown.\n";
        }
        std::cout << "PASS: direct mix-monitor lifecycle and disabled behavior.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
