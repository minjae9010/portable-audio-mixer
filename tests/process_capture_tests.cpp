// Hardware-only white-box check of the real asynchronous process capture path.
// Including the implementation keeps internal capture types out of the app API.
#include "../src/audio_engine.cpp"
#include <audiopolicy.h>
#include <iostream>
int main(int argc,char**) {
    try {
        ComApartment apartment;
        AudioEngine engine;
        // App-only startup must reach render validation without opening a mic.
        engine.start(L"",L"",L"missing-test-render-endpoint");
        const auto startupDeadline=GetTickCount64()+3000;
        while(engine.active.load() && GetTickCount64()<startupDeadline) Sleep(5);
        if(engine.active.load() || engine.statusText().find(L"Open VB-CABLE render endpoint")==std::wstring::npos)
            throw std::runtime_error("App-only startup unexpectedly required a microphone");
        engine.stop();
        ComPtr<IMMDeviceEnumerator> devices; ComPtr<IMMDevice> device;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices)),L"Test enumerator");
        check(devices->GetDefaultAudioEndpoint(eRender,eConsole,&device),L"Test default output");
        ComPtr<IAudioClient> client;
        check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(client.GetAddressOf())),L"Test render client");
        WAVEFORMATEX fmt{}; fmt.wFormatTag=WAVE_FORMAT_PCM; fmt.nChannels=2; fmt.nSamplesPerSec=mix::Rate;
        fmt.wBitsPerSample=16; fmt.nBlockAlign=4; fmt.nAvgBytesPerSec=mix::Rate*4;
        check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            1000000,0,&fmt,nullptr),L"Test render initialize");
        ComPtr<IAudioRenderClient> writer; ComPtr<ISimpleAudioVolume> volume;
        check(client->GetService(IID_PPV_ARGS(&writer)),L"Test writer");
        check(client->GetService(IID_PPV_ARGS(&volume)),L"Test session volume");
        float before=0; BOOL mutedBefore=FALSE;
        check(volume->GetMasterVolume(&before),L"Read original volume"); check(volume->GetMute(&mutedBefore),L"Read original mute");
        const auto epoch=qpc100ns();
        ProcessTap tap(engine,GetCurrentProcessId(),epoch);
        check(client->Start(),L"Start test playback");
        UINT32 capacity=0; check(client->GetBufferSize(&capacity),L"Test capacity");
        const auto deadline=GetTickCount64()+5000;
        uint64_t sent=0; bool received=false;
        while(GetTickCount64()<deadline) {
            UINT32 padding=0; check(client->GetCurrentPadding(&padding),L"Test padding");
            if(padding<=capacity && capacity-padding>=mix::BlockFrames) {
                BYTE* target=nullptr; check(writer->GetBuffer(mix::BlockFrames,&target),L"Test buffer");
                auto* pcm=reinterpret_cast<int16_t*>(target);
                for(int i=0;i<mix::BlockFrames;++i,++sent) {
                    const auto value=mix::toPcm(.01f*std::sin(2*3.141592653589793*997*static_cast<double>(sent)/mix::Rate));
                    pcm[2*i]=value; pcm[2*i+1]=value;
                }
                check(writer->ReleaseBuffer(mix::BlockFrames,0),L"Test release");
            }
            if(FAILED(tap.error.load())) throw Failure{tap.error.load(),tap.errorContext};
            const int64_t cursor=mix::samplePosition(qpc100ns(),epoch)-mix::Rate/10;
            if(cursor>0) {
                mix::Block raw; uint64_t late=0,missing=0,overflow=0;
                tap.take(cursor,raw,late,missing,overflow);
                float peak=0; for(const auto& f:raw) peak=std::max(peak,std::abs(f.l));
                if(peak>.001f) {
                    auto outgoing=raw; mix::InputFader muted(0); muted.apply(outgoing,0);
                    for(const auto& f:outgoing) if(f.l!=0 || f.r!=0) throw std::runtime_error("Outgoing mute leaked a sample");
                    received=true; break;
                }
            }
            Sleep(10);
        }
        client->Stop();
        if(!received) throw std::runtime_error("No process-loopback signal received; check muted/default test endpoint");
        float after=0; BOOL mutedAfter=FALSE;
        check(volume->GetMasterVolume(&after),L"Read final volume"); check(volume->GetMute(&mutedAfter),L"Read final mute");
        if(before!=after || mutedBefore!=mutedAfter) throw std::runtime_error("Native app session volume or mute changed");
        if(argc>1) {
            const auto adapters=virtual_mic::cables();
            if(adapters.size()!=1) throw std::runtime_error("One active managed cable is required for output smoke test");
            const auto render=virtual_mic::findEndpoint(adapters.front(),eRender);
            if(render.empty()) throw std::runtime_error("No active managed render endpoint");
            // No inputs or microphone: verify real virtual-output startup using
            // silence, without transmitting the user's app or microphone audio.
            engine.start(L"",L"",render);
            const auto deadline=GetTickCount64()+3000;
            while(engine.active.load() && !engine.output.active.load() && GetTickCount64()<deadline) Sleep(5);
            const bool started=engine.active.load() && engine.output.active.load();
            engine.stop();
            if(!started) throw std::runtime_error("Actual app-only virtual-microphone engine failed to start");
            std::cout<<"PASS: actual managed virtual-microphone engine startup without a microphone.\n";
        }
        std::cout<<"PASS: real process-loopback signal, independent outgoing mute and unchanged Windows session.\n";
        return 0;
    } catch(const Failure& f) { std::wcerr<<f.context<<L": HRESULT 0x"<<std::hex<<static_cast<uint32_t>(f.hr)<<L'\n'; return 1; }
      catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
