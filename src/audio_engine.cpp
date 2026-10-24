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
#include <map>
#include <tlhelp32.h>

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

// Both this callback and the audio worker belong to the MTA. Expose IAgileObject
// as well as the FTM: ActivateAudioInterfaceAsync checks callback agility.
// https://learn.microsoft.com/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-activateaudiointerfaceasync
// Aggregate the
// system free-threaded marshaler so the handler is agile without depending on
// MSVC-only WRL RuntimeClass. Its own event survives cancellation/timeouts.
class Activation final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
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
        if (iid == __uuidof(IAgileObject)) {
            *object = static_cast<IAgileObject*>(this); AddRef(); return S_OK;
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

// Each process starts independently, so adding/removing an app never stalls
// the mixer while Windows asynchronously activates a process loopback client.
class ProcessTap {
    AudioEngine& engine_;
    uint64_t epoch_;
    Handle stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread worker_;
    std::mutex mutex_;
    mix::Timeline timeline_;
    void run(DWORD pid) noexcept {
        try {
            ComApartment apartment;
            Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
            Capture capture; capture.client=processClient(pid,stop_); capture.initialize(true); capture.start();
            AudioScheduling scheduling;
            live=true;
            HANDLE waits[]{stop_,process,capture.event};
            for(;;) {
                const DWORD w=WaitForMultipleObjects(3,waits,FALSE,10);
                if(w==WAIT_OBJECT_0 || w==WAIT_OBJECT_0+1) break;
                if(w==WAIT_FAILED) throw Failure{HRESULT_FROM_WIN32(GetLastError()),L"Process input wait"};
                std::lock_guard<std::mutex> lock(mutex_); capture.drain(timeline_,epoch_,engine_);
            }
            capture.stop();
            std::lock_guard<std::mutex> lock(mutex_); capture.drain(timeline_,epoch_,engine_);
        } catch(const Failure& f) { if(f.hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED)) { errorContext=f.context; error=f.hr; } }
          catch(...) { error=E_FAIL; }
        live=false;
    }
public:
    std::atomic<bool> live{false};
    std::atomic<HRESULT> error{S_OK};
    std::wstring errorContext;
    ProcessTap(AudioEngine& engine,DWORD pid,uint64_t epoch) : engine_(engine),epoch_(epoch) {
        worker_=std::thread([this,pid]{run(pid);});
    }
    void stop() { SetEvent(stop_); if(worker_.joinable()) worker_.join(); }
    ~ProcessTap() { stop(); }
    void take(int64_t cursor,mix::Block& block,uint64_t& late,uint64_t& missing,uint64_t& overflow) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(size_t i=0;i<block.size();++i) block[i]=timeline_.take(cursor+static_cast<int64_t>(i));
        late+=timeline_.late; missing+=timeline_.missing; overflow+=timeline_.overflow;
    }
};
// Session enumeration can list a parent and its audio child. Capture their
// process tree once, otherwise the same audio would be mixed twice.
void deduplicateTrees(std::vector<mix::Input>& inputs) {
    std::map<DWORD,DWORD> parents;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot!=INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p{}; p.dwSize=sizeof(p);
        if(Process32FirstW(snapshot,&p)) do { parents[p.th32ProcessID]=p.th32ParentProcessID; } while(Process32NextW(snapshot,&p));
        CloseHandle(snapshot);
    }
    for(auto& input:inputs) {
        auto pids=input.pids;
        std::sort(pids.begin(),pids.end()); pids.erase(std::unique(pids.begin(),pids.end()),pids.end());
        input.pids.clear();
        for(DWORD pid:pids) {
            DWORD parent=pid; bool included=false;
            for(int depth=0;depth<128;++depth) {
                auto found=parents.find(parent); if(found==parents.end() || !found->second || found->second==parent) break;
                parent=found->second;
                if(std::find(pids.begin(),pids.end(),parent)!=pids.end()) { included=true; break; }
            }
            if(!included && pid && pid!=GetCurrentProcessId()) input.pids.push_back(pid);
        }
    }
}
struct InputGroup {
    mix::Input spec;
    mix::InputFader fader;
    std::vector<std::unique_ptr<ProcessTap>> taps;
    InputGroup(AudioEngine& engine,mix::Input input,uint64_t epoch) : spec(std::move(input)),fader(spec.control->target()) {
        for(DWORD pid:spec.pids) taps.push_back(std::make_unique<ProcessTap>(engine,pid,epoch));
        spec.control->armed=true;
    }
    ~InputGroup() { spec.control->armed=false; spec.control->active=false; spec.control->peak=0; }
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
void AudioEngine::start(std::wstring micId, std::wstring path, std::wstring outputId) {
    stop(); ResetEvent(stop_);
    clips = 0; late = 0; missing = 0; overflows = 0; discontinuities = 0; timestampErrors = 0;
    processPeak = 0; micPeak = 0; outputPeak = 0;
    output.peak=0; output.active=false;
    active = true; status(L"Starting capture...");
    try { thread_ = std::thread(&AudioEngine::run, this, std::move(micId), std::move(path), std::move(outputId)); }
    catch (...) { active = false; throw; }
}
void AudioEngine::stop() {
    SetEvent(stop_);
    if (thread_.joinable()) thread_.join();
}
void AudioEngine::run(std::wstring micId, std::wstring path, std::wstring outputId) noexcept {
    try {
        ComApartment apartment;
        Capture mic;
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&enumerator)), L"Create audio device enumerator");
        ComPtr<IMMDevice> device;
        if(!micId.empty()) {
            check(enumerator->GetDevice(micId.c_str(), &device), L"Open selected microphone");
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(mic.client.GetAddressOf())), L"Activate microphone");
            mic.initialize(false);
        }
        AudioScheduling scheduling;
        if (WaitForSingleObject(stop_, 0) == WAIT_OBJECT_0)
            throw Failure{HRESULT_FROM_WIN32(ERROR_CANCELLED), L"Stopped"};
        VirtualMicSink output(outputId);
        uint64_t outputDrops = 0;
        std::unique_ptr<DiskSink> disk;
        if (!path.empty()) disk = std::make_unique<DiskSink>(path);
        monitor.start(outputId);
        mix::Timeline micTimeline;
        mix::OutputFader outputFader(this->output.target());
        mix::InputFader appFader(processGain.load()), micFader(micGain.load());
        const uint64_t epoch = qpc100ns(); int64_t cursor = 0;
        std::map<std::wstring,std::unique_ptr<InputGroup>> groups;
        ULONGLONG nextRefresh=0;
        auto refreshInputs=[&] {
            auto selected=inputs.snapshot();
            deduplicateTrees(selected);
            for(auto it=groups.begin();it!=groups.end();) {
                auto found=std::find_if(selected.begin(),selected.end(),[&](const auto& input){return input.key==it->first;});
                if(found==selected.end() || found->pids!=it->second->spec.pids || found->control!=it->second->spec.control)
                    it=groups.erase(it);
                else ++it;
            }
            for(auto& input:selected) if(!groups.count(input.key)) {
                const auto key=input.key;
                groups.emplace(key,std::make_unique<InputGroup>(*this,std::move(input),epoch));
            }
        };
        auto render=[&](int64_t first,mix::Block& block) {
            mix::Block apps{}, raw{}, voice{};
            uint64_t inputLate=0,inputMissing=0,inputOverflow=0;
            for(auto& item:groups) {
                auto& group=*item.second; mix::Block source{};
                bool live=false,failed=false;
                for(auto& tap:group.taps) {
                    tap->take(first,raw,inputLate,inputMissing,inputOverflow); mix::add(source,raw);
                    live=live || tap->live.load(); failed=failed || FAILED(tap->error.load());
                }
                group.spec.control->active=live; group.spec.control->failed=failed;
                group.spec.control->peak=group.fader.apply(source,group.spec.control->target());
                mix::add(apps,source);
            }
            processPeak=appFader.apply(apps,processGain.load());
            float voicePeak=0;
            for(size_t i=0;mic.client && i<voice.size();++i) {
                voice[i]=micTimeline.take(first+static_cast<int64_t>(i));
                voicePeak=std::max(voicePeak,std::max(std::abs(voice[i].l),std::abs(voice[i].r)));
            }
            micPeak=voicePeak; micFader.apply(voice,micGain.load());
            block=apps; mix::add(block,voice); clips+=mix::limit(block);
            outputPeak=outputFader.apply(block,this->output); this->output.peak=outputPeak.load();
            late=inputLate+micTimeline.late; missing=inputMissing+micTimeline.missing;
            overflows=inputOverflow+micTimeline.overflow+outputDrops;
        };
        if(mic.client) mic.start();
        this->output.active=true;
        status(disk ? L"Mixing to virtual microphone and WAV." : L"Mixing to virtual microphone.");
        HANDLE waits[]{stop_,mic.event};
        for (;;) {
            const DWORD w=WaitForMultipleObjects(2,waits,FALSE,10);
            if(w==WAIT_OBJECT_0) break;
            if(w==WAIT_FAILED) throw Failure{HRESULT_FROM_WIN32(GetLastError()),L"Capture wait"};
            if(mic.client) mic.drain(micTimeline,epoch,*this);
            if(GetTickCount64()>=nextRefresh) { refreshInputs(); nextRefresh=GetTickCount64()+250; }
            const int64_t ready=mix::samplePosition(qpc100ns(),epoch)-mix::Rate*80/1000;
            if(ready-cursor>mix::Rate/2) throw std::runtime_error("Audio worker stalled over 500 ms; recording stopped");
            while(cursor+mix::BlockFrames<=ready) {
                mix::Block block; render(cursor,block);
                if(!output.push(block)) outputDrops+=mix::BlockFrames;
                monitor.push(block);
                if(disk) disk->push(block);
                cursor+=mix::BlockFrames;
            }
        }
        const int64_t end=mix::samplePosition(qpc100ns(),epoch);
        if(mic.client) { mic.stop(); mic.drain(micTimeline,epoch,*this); }
        for(auto& item:groups) for(auto& tap:item.second->taps) tap->stop();
        if(disk && end-cursor<=mix::Rate/2) {
            while(cursor<end) { mix::Block block; render(cursor,block); disk->push(block); cursor+=mix::BlockFrames; }
        }
        groups.clear();
        if(disk) disk->finish();
        if(WaitForSingleObject(stop_,0)==WAIT_OBJECT_0) status(disk ? L"Stopped. WAV finalized." : L"Stopped.");
    } catch (const Failure& f) {
        if (f.hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) status(L"Stopped.");
        else {
            std::wostringstream msg; msg << f.context << L" failed (HRESULT 0x" << std::hex
                                        << static_cast<uint32_t>(f.hr) << L"). Stop and retry after fixing the device/privacy setting.";
            status(msg.str());
        }
    } catch (const std::exception& e) { status(L"Error: " + widen(e.what())); }
      catch (...) { status(L"Unexpected audio error. Recording stopped."); }
    monitor.stop();
    output.peak=0; output.active=false; outputPeak=0;
    active = false;
}
