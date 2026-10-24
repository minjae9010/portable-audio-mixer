#include "audio_engine.hpp"
#include "virtual_microphone.hpp"
#include "wav_writer.hpp"
#include <audioclient.h>
#include "process_loopback_compat.hpp"
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <avrt.h>
#include <array>
#include <condition_variable>
#include <exception>
#include <memory>
#include <sstream>

using Microsoft::WRL::ComPtr;
namespace {
struct Failure { HRESULT hr; std::wstring context; };
void check(HRESULT hr, const wchar_t* context) { if (FAILED(hr)) throw Failure{hr, context}; }
class Handle {
    HANDLE h_;
public:
    explicit Handle(HANDLE h) : h_(h) {
        if (!h || h == INVALID_HANDLE_VALUE) throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Create handle"};
    }
    ~Handle() { CloseHandle(h_); }
    operator HANDLE() const { return h_; }
    Handle(const Handle&) = delete;
};
struct ComApartment {
    ComApartment() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), L"Initialize COM"); }
    ~ComApartment() { CoUninitialize(); }
};
struct AudioScheduling {
    HANDLE task = nullptr;
    AudioScheduling() { DWORD index = 0; task = AvSetMmThreadCharacteristicsW(L"Audio", &index); }
    ~AudioScheduling() { if (task) AvRevertMmThreadCharacteristics(task); }
};
class VirtualMicSink {
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioRenderClient> render_;
    UINT32 capacity_ = 0;
public:
    explicit VirtualMicSink(const std::wstring& endpointId) {
        ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDevice> device;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), L"Create render enumerator");
        check(enumerator->GetDevice(endpointId.c_str(), &device), L"Open VB-CABLE render endpoint");
        const auto adapters = virtual_mic::cables();
        if (adapters.size() != 1 || !virtual_mic::endpointBelongsTo(device.Get(), enumerator.Get(), adapters.front()))
            throw std::runtime_error("Output is not the managed VB-CABLE; refusing to send audio");
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(client_.GetAddressOf())), L"Activate VB-CABLE output");
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
        format.nSamplesPerSec = mix::Rate; format.wBitsPerSample = 16;
        format.nBlockAlign = 4; format.nAvgBytesPerSec = mix::Rate * 4;
        check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000, 0, &format, nullptr), L"Initialize VB-CABLE 48 kHz stereo");
        check(client_->GetBufferSize(&capacity_), L"Get VB-CABLE buffer size");
        check(client_->GetService(IID_PPV_ARGS(&render_)), L"Get VB-CABLE render service");
        BYTE* data = nullptr;
        const UINT32 prefill = (std::min)(capacity_, UINT32(mix::BlockFrames * 2));
        check(render_->GetBuffer(prefill, &data), L"Prime VB-CABLE buffer");
        check(render_->ReleaseBuffer(prefill, AUDCLNT_BUFFERFLAGS_SILENT), L"Prime VB-CABLE silence");
        check(client_->Start(), L"Start VB-CABLE output");
    }
    ~VirtualMicSink() { if (client_) { client_->Stop(); client_->Reset(); } }
    VirtualMicSink(const VirtualMicSink&) = delete;
    bool push(const mix::Block& block) {
        UINT32 padding = 0;
        check(client_->GetCurrentPadding(&padding), L"Read VB-CABLE buffer (device disconnected?)");
        // Keep realtime bounded if render/capture clocks drift. No worker sleeps
        // or blocking writes that could prevent stop or capture packet draining.
        if (padding > capacity_ || capacity_ - padding < mix::BlockFrames) return false;
        BYTE* data = nullptr;
        check(render_->GetBuffer(mix::BlockFrames, &data), L"Get VB-CABLE output buffer");
        auto* pcm = reinterpret_cast<int16_t*>(data);
        for (size_t i = 0; i < block.size(); ++i) {
            pcm[2*i] = mix::toPcm(block[i].l); pcm[2*i+1] = mix::toPcm(block[i].r);
        }
        check(render_->ReleaseBuffer(mix::BlockFrames, 0), L"Send mix to VB-CABLE");
        return true;
    }
};
uint64_t qpc100ns() {
    LARGE_INTEGER t{}, f{};
    QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
    return static_cast<uint64_t>(static_cast<long double>(t.QuadPart) * 10000000 / f.QuadPart);
}

// Both this callback and the audio worker belong to the MTA. Aggregate the
// system free-threaded marshaler so the handler is agile without depending on
// MSVC-only WRL RuntimeClass. Its own event survives cancellation/timeouts.
class Activation final : public IActivateAudioInterfaceCompletionHandler {
    std::atomic<ULONG> references_{1};
    ComPtr<IUnknown> marshaler_;
public:
    Activation() {
        check(CoCreateFreeThreadedMarshaler(static_cast<IActivateAudioInterfaceCompletionHandler*>(this),
            marshaler_.GetAddressOf()), L"Create activation callback marshaler");
    }
    STDMETHOD(QueryInterface)(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef(); return S_OK;
        }
        if (iid == __uuidof(IMarshal)) return marshaler_->QueryInterface(iid, object);
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return references_.fetch_add(1, std::memory_order_relaxed) + 1; }
    STDMETHOD_(ULONG, Release)() override {
        const ULONG left = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!left) delete this;
        return left;
    }
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    Handle done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* op) override {
        ComPtr<IUnknown> value;
        HRESULT activation = E_FAIL;
        result = op->GetActivateResult(&activation, &value);
        if (SUCCEEDED(result)) result = activation;
        if (SUCCEEDED(result)) result = value ? value.As(&client) : E_POINTER;
        SetEvent(done);
        return S_OK;
    }
};
ComPtr<IAudioClient> processClient(DWORD pid, HANDLE stop) {
    ComPtr<Activation> completion;
    completion.Attach(new Activation());
    auto& params = completion->params;
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT prop{};
    prop.vt = VT_BLOB;
    prop.blob.cbSize = sizeof(params);
    prop.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
        __uuidof(IAudioClient), &prop, completion.Get(), &operation), L"Activate process loopback (Windows build 20348+)" );
    HANDLE waits[]{stop, completion->done};
    const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 10000);
    if (w == WAIT_OBJECT_0) throw Failure{HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Stopped"};
    if (w == WAIT_TIMEOUT) throw Failure{HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Process loopback activation timed out"};
    if (w != WAIT_OBJECT_0+1) throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Activation wait failed"};
    check(completion->result, L"Process loopback activation result");
    return completion->client;
}
struct Capture {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    Handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    int64_t next = 0;
    bool haveNext = false, started = false;
    void initialize(bool loopback) {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2;
        fmt.nSamplesPerSec = mix::Rate; fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = mix::Rate * 4;
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
            (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, &fmt, nullptr),
            loopback ? L"Initialize process loopback PCM 48 kHz stereo" : L"Initialize microphone PCM 48 kHz stereo (shared conversion)");
        check(client->SetEventHandle(event), L"Set capture event");
        check(client->GetService(IID_PPV_ARGS(&capture)), L"Get capture service");
    }
    void start() { check(client->Start(), L"Start capture"); started = true; }
    void stop() { if (started) { client->Stop(); started = false; } }
    ~Capture() { stop(); capture.Reset(); client.Reset(); }
    void drain(mix::Timeline& timeline, uint64_t epoch, AudioEngine& engine) {
        UINT32 frames = 0;
        check(capture->GetNextPacketSize(&frames), L"Read capture packet size (device disconnected?)");
        // Bound work so a pathological source cannot prevent stop handling.
        for (int packet = 0; frames && packet < 256; ++packet) {
            BYTE* bytes = nullptr; DWORD flags = 0; UINT64 position = 0, stamp = 0;
            check(capture->GetBuffer(&bytes, &frames, &flags, &position, &stamp), L"Read capture buffer");
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++engine.discontinuities;
            const bool badTime = (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0;
            if (badTime) ++engine.timestampErrors;
            const int64_t first = badTime ? (haveNext ? next : mix::samplePosition(qpc100ns(), epoch) - frames)
                                          : mix::samplePosition(stamp, epoch);
            timeline.put(first, reinterpret_cast<const int16_t*>(bytes), frames,
                         (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || bytes == nullptr);
            next = first + frames; haveNext = true;
            check(capture->ReleaseBuffer(frames), L"Release capture packet");
            check(capture->GetNextPacketSize(&frames), L"Read next capture packet size");
        }
    }
};

// Disk I/O stays off the capture thread. A bounded 1.28 s queue fails visibly
// on sustained slow storage rather than allocating without limit.
class DiskSink {
    mix::WavWriter writer_;
    std::array<mix::Block, 128> queue_{};
    size_t head_ = 0, count_ = 0;
    bool stopping_ = false;
    std::exception_ptr failure_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread worker_;
    void work() noexcept {
        try {
            for (;;) {
                mix::Block block;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [&]{ return count_ || stopping_; });
                    if (!count_) break;
                    block = queue_[head_]; head_ = (head_+1) % queue_.size(); --count_;
                }
                writer_.write(block);
            }
            writer_.finish();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_); failure_ = std::current_exception();
        }
    }
public:
    explicit DiskSink(const std::wstring& path) : writer_(std::filesystem::path(path)), worker_([this]{ work(); }) {}
    void push(const mix::Block& block) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_) std::rethrow_exception(failure_);
        if (count_ == queue_.size()) throw std::runtime_error("Disk queue full; recording stopped");
        queue_[(head_+count_) % queue_.size()] = block; ++count_; ready_.notify_one();
    }
    void finish() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
        if (failure_) std::rethrow_exception(failure_);
    }
    ~DiskSink() { try { finish(); } catch (...) {} }
};
std::wstring widen(const char* s) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) return L"Unknown error";
    std::wstring out(len, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    out.pop_back(); return out;
}
}

AudioEngine::AudioEngine() {
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_) throw std::runtime_error("Cannot create stop event");
}
AudioEngine::~AudioEngine() { stop(); CloseHandle(stop_); }
void AudioEngine::status(std::wstring text) { std::lock_guard<std::mutex> lock(textMutex_); text_ = std::move(text); }
std::wstring AudioEngine::statusText() const { std::lock_guard<std::mutex> lock(textMutex_); return text_; }
void AudioEngine::start(DWORD pid, std::wstring micId, std::wstring path, std::wstring outputId) {
    stop(); ResetEvent(stop_);
    clips = 0; late = 0; missing = 0; overflows = 0; discontinuities = 0; timestampErrors = 0;
    processPeak = 0; micPeak = 0; outputPeak = 0;
    output.peak=0; output.active=false;
    active = true; status(L"Starting capture...");
    try { thread_ = std::thread(&AudioEngine::run, this, pid, std::move(micId), std::move(path), std::move(outputId)); }
    catch (...) { active = false; throw; }
}
void AudioEngine::stop() {
    SetEvent(stop_);
    if (thread_.joinable()) thread_.join();
}
void AudioEngine::run(DWORD pid, std::wstring micId, std::wstring path, std::wstring outputId) noexcept {
    try {
        ComApartment apartment;
        // Retain a process handle to detect exit and avoid silently tracking a reused PID.
        Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        Capture app, mic;
        app.client = processClient(pid, stop_);
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&enumerator)), L"Create audio device enumerator");
        ComPtr<IMMDevice> device;
        check(enumerator->GetDevice(micId.c_str(), &device), L"Open selected microphone");
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(mic.client.GetAddressOf())), L"Activate microphone");
        app.initialize(true); mic.initialize(false);
        AudioScheduling scheduling;
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
            throw Failure{HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Stopped"};
        VirtualMicSink output(outputId);
        uint64_t outputDrops = 0;
        std::unique_ptr<DiskSink> disk;
        if (!path.empty()) disk = std::make_unique<DiskSink>(path);
        mix::Timeline processTimeline, micTimeline; mix::Mixer mixer; mix::Block block;
        mix::OutputFader outputFader(this->output.target());
        const uint64_t epoch = qpc100ns(); int64_t cursor = 0;
        app.start(); mic.start();
        this->output.active=true;
        status(disk ? L"Mixing to virtual microphone and WAV." : L"Mixing to virtual microphone.");
        HANDLE waits[]{stop_, process, app.event, mic.event};
        for (;;) {
            const DWORD w = WaitForMultipleObjects(4, waits, FALSE, 10);
            if (w == WAIT_OBJECT_0) break;
            if (w == WAIT_OBJECT_0+1) { status(L"Target process exited. Mix stopped."); break; }
            if (w == WAIT_FAILED) throw Failure{HRESULT_FROM_WIN32(GetLastError()), L"Capture wait"};
            app.drain(processTimeline, epoch, *this); mic.drain(micTimeline, epoch, *this);
            // Packet QPC timestamps share a clock. Keep 80 ms for packet delivery
            // and scheduling jitter; gaps are silence, stale packets are discarded.
            const int64_t ready = mix::samplePosition(qpc100ns(), epoch) - mix::Rate*80/1000;
            if (ready - cursor > mix::Rate/2)
                throw std::runtime_error("Audio worker stalled over 500 ms; recording stopped");
            while (cursor + mix::BlockFrames <= ready) {
                const auto meter = mixer.render(processTimeline, micTimeline, cursor,
                    processGain.load(), micGain.load(), block);
                processPeak = meter.process; micPeak = meter.mic;
                outputPeak=outputFader.apply(block,this->output); this->output.peak=outputPeak.load();
                clips += meter.clipped;
                if (!output.push(block)) outputDrops += mix::BlockFrames;
                if (disk) disk->push(block);
                cursor += mix::BlockFrames;
            }
            late = processTimeline.late + micTimeline.late;
            missing = processTimeline.missing + micTimeline.missing;
            overflows = processTimeline.overflow + micTimeline.overflow + outputDrops;
        }
        app.stop(); mic.stop();
        // Drain final packets and flush through the stop time, including the
        // normal 80 ms lookahead, so short recordings keep their final audio.
        app.drain(processTimeline, epoch, *this); mic.drain(micTimeline, epoch, *this);
        const int64_t end = mix::samplePosition(qpc100ns(), epoch);
        if (disk && end - cursor <= mix::Rate/2) {
            while (cursor < end) {
                mixer.render(processTimeline, micTimeline, cursor, processGain.load(), micGain.load(), block);
                outputFader.apply(block,this->output);
                disk->push(block); cursor += mix::BlockFrames;
            }
        }
        if (disk) disk->finish();
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0) status(disk ? L"Stopped. WAV finalized." : L"Stopped.");
    } catch (const Failure& f) {
        if (f.hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) status(L"Stopped.");
        else {
            std::wostringstream msg; msg << f.context << L" failed (HRESULT 0x" << std::hex
                                        << static_cast<uint32_t>(f.hr) << L"). Stop and retry after fixing the device/privacy setting.";
            status(msg.str());
        }
    } catch (const std::exception& e) { status(L"Error: " + widen(e.what())); }
      catch (...) { status(L"Unexpected audio error. Recording stopped."); }
    output.peak=0; output.active=false; outputPeak=0;
    active = false;
}
