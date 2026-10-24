#include "loopback_monitor.hpp"
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char**) {
    try {
        LoopbackMonitor monitor;
        monitor.stop(); monitor.stop();
        monitor.start(L"same-endpoint", L"same-endpoint");
        require(!monitor.active.load(), "Same endpoint must not start a feedback loop");
        monitor.start(L"", L"destination");
        require(!monitor.active.load(), "Missing source must not start");
        monitor.start(L"source", L"");
        require(!monitor.active.load(), "Missing destination must not start");
        for (int i = 0; i < 3; ++i) {
            monitor.start(L"nonexistent-source", L"nonexistent-destination");
            const auto deadline = GetTickCount64() + 3000;
            while (monitor.active.load() && GetTickCount64() < deadline) Sleep(5);
            require(!monitor.active.load(), "Invalid device worker must terminate");
            require(monitor.statusText().find(L"0x") != std::wstring::npos, "Device errors must show HRESULT");
            monitor.stop();
        }
        // Destruction must join even when start is immediately followed by stop.
        { LoopbackMonitor immediate; immediate.start(L"missing-a", L"missing-b"); }
        if (argc > 1) {
            // Opt-in hardware smoke test. No recording or system-volume changes.
            using Microsoft::WRL::ComPtr;
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            require(SUCCEEDED(initialized), "Initialize hardware test COM");
            struct Cleanup { ~Cleanup() { CoUninitialize(); } } cleanup;
            ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> devices;
            require(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator))), "Create hardware enumerator");
            require(SUCCEEDED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)), "Enumerate hardware");
            ComPtr<IMMDevice> destination;
            require(SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &destination)), "Get default playback device");
            LPWSTR raw = nullptr; require(SUCCEEDED(destination->GetId(&raw)), "Get destination ID");
            std::wstring destinationId(raw); CoTaskMemFree(raw);
            UINT count = 0; devices->GetCount(&count);
            std::wstring sourceId;
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&raw))) continue;
                std::wstring id(raw); CoTaskMemFree(raw);
                if (id != destinationId) { sourceId = id; break; }
            }
            require(!sourceId.empty(), "Hardware smoke requires two distinct active render endpoints");
            monitor.start(sourceId, destinationId);
            const auto deadline = GetTickCount64() + 3000;
            while (monitor.active.load() && monitor.statusText().find(L"연결 중") != std::wstring::npos && GetTickCount64() < deadline) Sleep(5);
            require(monitor.active.load() && monitor.statusText().find(L"듣는 중") != std::wstring::npos,
                "Actual WASAPI loopback and render streams must start");
            Sleep(300);
            require(monitor.active.load(), "Hardware streams must remain running");
            const auto stopTime = GetTickCount64(); monitor.stop();
            require(!monitor.active.load() && GetTickCount64()-stopTime < 1000, "Hardware stop must join promptly");
            std::cout << "Actual endpoint loopback/render startup, running and stop passed.\n";
        }
        std::cout << "Loopback validation, device failure, restart and shutdown passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
