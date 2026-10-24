#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include "session_meter_compat.hpp"
#include "mix_output.hpp"
#include <memory>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cwctype>

namespace sessions {
using Microsoft::WRL::ComPtr;
struct Channel {
    std::wstring id, name, device, application;
    DWORD pid = 0;
    std::vector<ComPtr<ISimpleAudioVolume>> volumes;
    std::vector<ComPtr<IAudioMeterInformation>> meters;
    mix::OutputControl* output=nullptr;
    std::shared_ptr<mix::OutputControl> inputControl;
    std::vector<uint32_t> pids;
    bool master = false;
    bool playing = false;
    float level = 1, peak = 0;
    bool muted = false;
    bool online() const { return output || !volumes.empty(); }
    void read() {
        peak = 0; muted = true;
        if(output) {
            level=std::clamp(mix::finiteSample(output->gain.load()),0.0f,1.0f); muted=output->muted.load();
            playing=output->active.load(); peak=playing ? output->peak.load() : 0;
            device=master ? (playing ? L"가상 마이크 · 송출 중" : L"송출 시작 전")
                          : !output->armed.load() ? L"송출 시작 전"
                          : output->failed.load() ? L"프로그램 캡처 오류"
                          : playing ? L"프로그램 캡처 중"
                          : pids.empty() ? L"프로그램 오디오 없음" : L"프로그램 연결 중";
            return;
        }
        float sum = 0; unsigned count = 0;
        for (auto& v : volumes) {
            float l = 0; BOOL m = FALSE;
            if (SUCCEEDED(v->GetMasterVolume(&l))) { sum += l; ++count; }
            if (SUCCEEDED(v->GetMute(&m)) && !m) muted = false;
        }
        if (count) level = sum / count;
        for (auto& m : meters) { float p = 0; if (SUCCEEDED(m->GetPeakValue(&p))) peak = std::max(peak, p); }
    }
    HRESULT setLevel(float value) {
        if(output) { output->gain=std::clamp(mix::finiteSample(value),0.0f,1.0f); read(); return S_OK; }
        return E_ACCESSDENIED;
    }
    HRESULT setMute(bool value) {
        if(output) { output->muted=value; read(); return S_OK; }
        return E_ACCESSDENIED;
    }
};
inline std::wstring processPath(DWORD pid) {
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t path[32768]{}; DWORD size = 32768;
    bool ok = p && QueryFullProcessImageNameW(p, 0, path, &size);
    if (p) CloseHandle(p);
    return ok ? std::wstring(path) : L"";
}
inline std::wstring processName(DWORD pid) {
    if (!pid) return L"시스템 소리";
    std::wstring name=processPath(pid);
    if(name.empty()) return L"프로그램 " + std::to_wstring(pid);
    auto slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    return name;
}
inline std::wstring applicationKey(const Channel& channel) {
    std::wstring key=channel.application;
    if(key.empty()) return L"pid:"+std::to_wstring(channel.pid)+L":"+channel.name;
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return key;
}
inline HRESULT enumerate(std::vector<Channel>& result) {
    result.clear(); ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> devices;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
    if (FAILED(hr)) return hr;
    UINT count = 0; hr = devices->GetCount(&count); if (FAILED(hr)) return hr;
    HRESULT lastError = S_OK; unsigned scanned = 0;
    for (UINT d = 0; d < count; ++d) {
        ComPtr<IMMDevice> device; ComPtr<IAudioSessionManager2> manager;
        if (FAILED(hr = devices->Item(d, &device))) { lastError = hr; continue; }
        if (FAILED(hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(manager.GetAddressOf())))) { lastError = hr; continue; }
        ComPtr<IAudioSessionEnumerator> list;
        if (FAILED(hr = manager->GetSessionEnumerator(&list))) { lastError = hr; continue; }
        int n = 0; if (FAILED(hr = list->GetCount(&n))) { lastError = hr; continue; }
        ++scanned;
        LPWSTR rawId = nullptr; if (FAILED(device->GetId(&rawId))) continue;
        std::wstring deviceId(rawId); CoTaskMemFree(rawId);
        std::wstring deviceName = L"출력 장치";
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT name{};
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal)
                deviceName = name.pwszVal;
            PropVariantClear(&name);
        }
        for (int i = 0; i < n; ++i) {
            ComPtr<IAudioSessionControl> control; ComPtr<IAudioSessionControl2> info;
            ComPtr<ISimpleAudioVolume> volume; ComPtr<IAudioMeterInformation> meter;
            if (FAILED(list->GetSession(i, &control)) || FAILED(control.As(&info)) || FAILED(control.As(&volume))) continue;
            AudioSessionState state = AudioSessionStateExpired;
            if (FAILED(control->GetState(&state)) || state == AudioSessionStateExpired) continue;
            DWORD pid = 0;
            if (FAILED(info->GetProcessId(&pid)) || pid == GetCurrentProcessId()) continue;
            std::wstring id = deviceId + L":" + std::to_wstring(pid);
            auto found = std::find_if(result.begin(), result.end(), [&](const Channel& c) { return c.id == id; });
            if (found == result.end()) {
                Channel c; c.id = id; c.pid = pid; c.name = processName(pid); c.device = deviceName;
                c.application=processPath(pid);
                result.push_back(std::move(c)); found = result.end() - 1;
            }
            found->volumes.push_back(volume);
            if (SUCCEEDED(control.As(&meter))) found->meters.push_back(meter);
            found->playing = found->playing || state == AudioSessionStateActive;
        }
    }
    std::sort(result.begin(), result.end(), [](const Channel& a, const Channel& b) {
        int cmp = _wcsicmp(a.name.c_str(), b.name.c_str()); return cmp ? cmp < 0 : a.id < b.id;
    });
    for (auto& c : result) c.read();
    return scanned || !count ? S_OK : lastError;
}
} // namespace sessions
