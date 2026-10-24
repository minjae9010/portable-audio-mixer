#include "mix_monitor.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
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
struct Speakers {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> writer;
    UINT32 capacity = 0;
    explicit Speakers(const std::wstring& excludedEndpoint) {
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
        check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device));
        LPWSTR raw = nullptr; check(device->GetId(&raw));
        const std::wstring id(raw); CoTaskMemFree(raw);
        if (id == excludedEndpoint) throw Failure{E_INVALIDARG};
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client.GetAddressOf())));
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = mix::Rate; format.wBitsPerSample = 16;
        format.nBlockAlign = 4; format.nAvgBytesPerSec = mix::Rate * 4;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, &format, nullptr));
        check(client->GetBufferSize(&capacity));
        check(client->GetService(IID_PPV_ARGS(&writer)));
        check(client->Start());
    }
    ~Speakers() { if (client) client->Stop(); }
    bool writable() {
        UINT32 padding = 0; check(client->GetCurrentPadding(&padding));
        return padding <= capacity && capacity-padding >= mix::BlockFrames;
    }
    void push(const mix::Block& block) {
        BYTE* target = nullptr; check(writer->GetBuffer(mix::BlockFrames, &target));
        auto* pcm = reinterpret_cast<int16_t*>(target);
        for (size_t i = 0; i < block.size(); ++i) {
            pcm[2*i] = mix::toPcm(block[i].l); pcm[2*i+1] = mix::toPcm(block[i].r);
        }
        check(writer->ReleaseBuffer(mix::BlockFrames, 0));
    }
};
}
MixMonitor::MixMonitor() : wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
    if (!wake_) throw std::runtime_error("Cannot create mix monitor event");
}
MixMonitor::~MixMonitor() { stop(); CloseHandle(wake_); }
void MixMonitor::status(std::wstring text) { std::lock_guard<std::mutex> lock(mutex_); text_ = std::move(text); }
std::wstring MixMonitor::statusText() const {
    if (!enabled.load()) return L"믹스 듣기 꺼짐 · 가상 마이크 송출은 유지됩니다.";
    if (!running_.load()) return L"믹스를 시작하면 같은 소리를 Windows 기본 스피커로 들려줍니다.";
    std::lock_guard<std::mutex> lock(mutex_); return text_;
}
void MixMonitor::enable(bool value) {
    { std::lock_guard<std::mutex> lock(mutex_); head_ = count_ = 0; }
    enabled = value; failed_ = false; SetEvent(wake_);
}
void MixMonitor::start(std::wstring excludedEndpoint) {
    stop(); stopping_ = false; failed_ = false; dropped = 0;
    status(L"스피커 연결 중…"); running_ = true;
    try { worker_ = std::thread(&MixMonitor::run, this, std::move(excludedEndpoint)); }
    catch (...) { running_ = false; status(L"스피커 듣기를 시작하지 못했습니다."); }
}
void MixMonitor::push(const mix::Block& block) {
    if (!enabled.load() || !active.load()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == queue_.size()) { head_ = (head_+1)%queue_.size(); --count_; ++dropped; }
        queue_[(head_+count_)%queue_.size()] = block; ++count_;
    }
    SetEvent(wake_);
}
void MixMonitor::stop() {
    stopping_ = true; SetEvent(wake_);
    if (worker_.joinable()) worker_.join();
    active = false; running_ = false;
    std::lock_guard<std::mutex> lock(mutex_); head_ = count_ = 0;
}
void MixMonitor::run(std::wstring excludedEndpoint) noexcept {
    try {
        Apartment apartment;
        std::unique_ptr<Speakers> speakers;
        while (!stopping_.load()) {
            if (!enabled.load()) {
                speakers.reset(); active = false;
                { std::lock_guard<std::mutex> lock(mutex_); head_ = count_ = 0; }
            } else if (!failed_.load()) {
                try {
                    if (!speakers) { speakers = std::make_unique<Speakers>(excludedEndpoint); active = true; status(L"최종 믹스를 Windows 기본 스피커로 듣는 중"); }
                    while (enabled.load() && !stopping_.load() && speakers->writable()) {
                        mix::Block block;
                        { std::lock_guard<std::mutex> lock(mutex_);
                          if (!count_) break;
                          block = queue_[head_]; head_ = (head_+1)%queue_.size(); --count_; }
                        speakers->push(block);
                    }
                } catch (const Failure& f) {
                    speakers.reset(); active = false; failed_ = true;
                    if (f.hr == E_INVALIDARG) status(L"Windows 기본 출력을 가상 마이크 대신 실제 스피커로 설정하세요.");
                    else { std::wostringstream text; text << L"스피커 연결 실패 (0x" << std::hex << static_cast<unsigned long>(f.hr) << L"). 듣기를 껐다 켜서 다시 시도하세요."; status(text.str()); }
                }
            }
            WaitForSingleObject(wake_, 5);
        }
    } catch (...) { status(L"스피커 듣기 오류 · 가상 마이크 송출은 계속됩니다."); }
    active = false; running_ = false;
}
