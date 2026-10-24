#include "loopback_monitor.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace {
struct Failure { HRESULT hr; };
void check(HRESULT hr) { if (FAILED(hr)) throw Failure{hr}; }
struct Apartment {
    Apartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~Apartment() { CoUninitialize(); }
};
struct Stream {
    ComPtr<IAudioClient> client;
    ~Stream() { if (client) client->Stop(); }
};
}
LoopbackMonitor::LoopbackMonitor() : stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!stop_) throw std::runtime_error("Cannot create monitor stop event");
}
LoopbackMonitor::~LoopbackMonitor() { stop(); CloseHandle(stop_); }
void LoopbackMonitor::status(std::wstring text) {
    std::lock_guard<std::mutex> lock(mutex_); status_ = std::move(text);
}
std::wstring LoopbackMonitor::statusText() const {
    std::lock_guard<std::mutex> lock(mutex_); return status_;
}
void LoopbackMonitor::stop() {
    SetEvent(stop_);
    if (worker_.joinable()) worker_.join();
}
void LoopbackMonitor::start(std::wstring source, std::wstring destination) {
    stop();
    if (source.empty() || destination.empty() || source == destination) {
        status(L"반복 재생을 막기 위해 캡처 장치와 다른 듣기 장치를 선택하세요.");
        return;
    }
    ResetEvent(stop_); active = true; status(L"루프백 연결 중…");
    try { worker_ = std::thread(&LoopbackMonitor::run, this, std::move(source), std::move(destination)); }
    catch (...) { active = false; status(L"루프백 작업을 시작하지 못했습니다."); throw; }
}
void LoopbackMonitor::run(std::wstring source, std::wstring destination) noexcept {
    try {
        Apartment apartment;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDevice> input, output;
        check(enumerator->GetDevice(source.c_str(), &input));
        check(enumerator->GetDevice(destination.c_str(), &output));
        Stream capture, playback;
        check(input->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(capture.client.GetAddressOf())));
        check(output->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(playback.client.GetAddressOf())));
        WAVEFORMATEX* raw = nullptr;
        check(capture.client->GetMixFormat(&raw));
        std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> format(raw, CoTaskMemFree);
        check(capture.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
            1000000, 0, format.get(), nullptr));
        check(playback.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, format.get(), nullptr));
        ComPtr<IAudioCaptureClient> reader;
        ComPtr<IAudioRenderClient> writer;
        check(capture.client->GetService(IID_PPV_ARGS(&reader)));
        check(playback.client->GetService(IID_PPV_ARGS(&writer)));
        UINT32 capacity = 0;
        check(playback.client->GetBufferSize(&capacity));
        check(playback.client->Start());
        check(capture.client->Start());
        status(L"루프백 듣는 중 · 헤드폰 사용 권장");
        while (WaitForSingleObject(stop_, 5) == WAIT_TIMEOUT) {
            // Detect a removed playback device even while the source is silent.
            UINT32 currentPadding = 0;
            check(playback.client->GetCurrentPadding(&currentPadding));
            UINT32 frames = 0;
            check(reader->GetNextPacketSize(&frames));
            // Bounded draining, no disk I/O and no unbounded queue on clock drift.
            for (int packet = 0; frames && packet < 128; ++packet) {
                BYTE* data = nullptr; DWORD flags = 0;
                check(reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr));
                UINT32 padding = 0;
                HRESULT hr = playback.client->GetCurrentPadding(&padding);
                const UINT32 count = SUCCEEDED(hr) && padding <= capacity ? std::min(frames, capacity-padding) : 0;
                if (count) {
                    BYTE* target = nullptr;
                    hr = writer->GetBuffer(count, &target);
                    if (SUCCEEDED(hr)) {
                        const bool silent = !data || (flags & AUDCLNT_BUFFERFLAGS_SILENT);
                        if (!silent) std::memcpy(target, data, static_cast<size_t>(count)*format->nBlockAlign);
                        hr = writer->ReleaseBuffer(count, silent ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
                    }
                }
                const HRESULT released = reader->ReleaseBuffer(frames);
                check(hr); check(released);
                check(reader->GetNextPacketSize(&frames));
                if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) break;
            }
        }
        status(L"루프백 듣기 중지됨.");
    } catch (const Failure& failure) {
        std::wostringstream text;
        text << L"루프백 연결 실패 (0x" << std::hex << static_cast<unsigned long>(failure.hr)
             << L"). 장치를 새로고침하고 다시 선택하세요.";
        status(text.str());
    } catch (...) { status(L"루프백 듣기 오류. 장치를 확인하고 다시 시작하세요."); }
    active = false;
}
