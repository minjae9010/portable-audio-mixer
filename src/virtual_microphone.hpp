#pragma once
#include <windows.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cwchar>
#include <cstdint>

namespace virtual_mic {
using Microsoft::WRL::ComPtr;
// Vendor Pack45 INF identifiers; never select devices by their display name.
inline bool isCable(DEVINST node) {
    wchar_t service[256]{}; ULONG bytes = sizeof(service), type = 0;
    if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_SERVICE, &type, service, &bytes, 0) != CR_SUCCESS ||
        type != REG_SZ || _wcsicmp(service, L"VBAudioVACMME")) return false;
    wchar_t ids[4096]{}; bytes = sizeof(ids)-sizeof(wchar_t);
    if (CM_Get_DevNode_Registry_PropertyW(node, CM_DRP_HARDWAREID, &type, ids, &bytes, 0) != CR_SUCCESS || type != REG_MULTI_SZ)
        return false;
    for (const wchar_t* p = ids; *p; p += wcslen(p)+1)
        if (!_wcsicmp(p, L"VBAudioVACWDM")) return true;
    return false;
}
inline std::vector<std::wstring> cables() {
    ULONG count = 0;
    if (CM_Get_Device_ID_List_SizeW(&count, nullptr, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS) return {};
    std::vector<wchar_t> list(count+2, 0);
    if (CM_Get_Device_ID_ListW(nullptr, list.data(), count, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS) return {};
    std::vector<std::wstring> result;
    for (wchar_t* p = list.data(); *p; p += wcslen(p)+1) {
        DEVINST node = 0;
        if (CM_Locate_DevNodeW(&node, p, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS && isCable(node)) result.emplace_back(p);
    }
    return result;
}
inline CONFIGRET change(const std::wstring& id, bool enable) {
    DEVINST node = 0;
    auto cr = CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL);
    if (cr != CR_SUCCESS) return cr;
    if (!isCable(node)) return CR_INVALID_DEVNODE;
    return enable ? CM_Enable_DevNode(node, 0) : CM_Disable_DevNode(node, CM_DISABLE_UI_NOT_OK | CM_DISABLE_PERSIST);
}
inline bool belongsTo(IMMDevice* device, const std::wstring& adapter) {
    ComPtr<IPropertyStore> props; PROPVARIANT value{};
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) return false;
    bool match = false;
    if (SUCCEEDED(props->GetValue(PKEY_Device_InstanceId, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        DEVINST node = 0;
        if (CM_Locate_DevNodeW(&node, value.pwszVal, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
            for (unsigned depth = 0; depth < 16; ++depth) {
                wchar_t id[MAX_DEVICE_ID_LEN]{};
                if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS && !_wcsicmp(id, adapter.c_str())) {
                    match = true; break;
                }
                DEVINST parent = 0;
                if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
                node = parent;
            }
        }
    }
    PropVariantClear(&value);
    return match;
}
inline bool endpointBelongsTo(IMMDevice* endpoint, IMMDeviceEnumerator* enumerator, const std::wstring& adapter) {
    if (belongsTo(endpoint, adapter)) return true;
    ComPtr<IDeviceTopology> topology; ComPtr<IConnector> connector;
    if (FAILED(endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(topology.GetAddressOf()))) || FAILED(topology->GetConnector(0, &connector))) return false;
    LPWSTR connected = nullptr;
    if (FAILED(connector->GetDeviceIdConnectedTo(&connected))) return false;
    ComPtr<IMMDevice> device;
    const HRESULT hr = enumerator->GetDevice(connected, &device);
    CoTaskMemFree(connected);
    return SUCCEEDED(hr) && belongsTo(device.Get(), adapter);
}
inline std::wstring findEndpoint(const std::wstring& adapter, EDataFlow flow) {
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> collection;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) return {};
    UINT count = 0; collection->GetCount(&count);
    std::wstring result;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> endpoint;
        if (FAILED(collection->Item(i, &endpoint)) || !endpointBelongsTo(endpoint.Get(), enumerator.Get(), adapter)) continue;
        LPWSTR id = nullptr;
        if (SUCCEEDED(endpoint->GetId(&id))) {
            if (!result.empty()) { CoTaskMemFree(id); return {}; }
            result = id; CoTaskMemFree(id);
            if (flow == eCapture) break; // Pack45 also exposes a multichannel capture endpoint.
        }
    }
    return result;
}
struct GuardState { DWORD result; wchar_t instance[MAX_DEVICE_ID_LEN]; };
constexpr DWORD Pending = 0xffffffff, Missing = 0xfffffffe, Busy = 0xfffffffd, Ambiguous = 0xfffffffc;
// Parent identity is an inherited process handle, never a reusable PID.
template<class Enumerate, class Change, class Report>
int runGuardWith(HANDLE parent, HANDLE stop, HANDLE ready, HANDLE mapping,
                 const wchar_t* leaseName, Enumerate enumerate, Change setState, Report report) {
    auto* state = static_cast<GuardState*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(GuardState)));
    if (!state) return 2;
    HANDLE mutex = CreateMutexW(nullptr, FALSE, leaseName);
    DWORD result = Busy; bool acquired = false;
    if (mutex) {
        const DWORD wait = WaitForSingleObject(mutex, 0);
        acquired = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }
    std::wstring id;
    if (acquired) {
        auto found = enumerate();
        result = found.empty() ? Missing : (found.size() != 1 ? Ambiguous : CR_SUCCESS);
        if (result == CR_SUCCESS) {
            id = found.front(); wcsncpy_s(state->instance, id.c_str(), _TRUNCATE);
            HANDLE waits[]{parent, stop};
            result = WaitForMultipleObjects(2, waits, FALSE, 0) == WAIT_TIMEOUT ? setState(id, true) : CR_FAILURE;
        }
    }
    state->result = result; SetEvent(ready);
    if (result == CR_SUCCESS) {
        HANDLE waits[]{parent, stop}; WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    }
    DWORD disabled = CR_SUCCESS;
    if (!id.empty()) {
        for (unsigned attempt = 0; attempt < 5; ++attempt) {
            disabled = setState(id, false);
            if (disabled == CR_SUCCESS || disabled == CR_NO_SUCH_DEVNODE) break;
            Sleep(200);
        }
        if (disabled != CR_SUCCESS && disabled != CR_NO_SUCH_DEVNODE)
            report(disabled);
    }
    if (acquired) ReleaseMutex(mutex);
    if (mutex) CloseHandle(mutex);
    UnmapViewOfFile(state);
    CloseHandle(parent); CloseHandle(stop); CloseHandle(ready); CloseHandle(mapping);
    return disabled == CR_SUCCESS || disabled == CR_NO_SUCH_DEVNODE ? 0 : 3;
}
inline int runGuard(HANDLE parent, HANDLE stop, HANDLE ready, HANDLE mapping) {
    return runGuardWith(parent, stop, ready, mapping, L"Global\\PortableMixer.VBCable.Lease.v1", cables, change,
        [](DWORD) { MessageBoxW(nullptr,
            L"VB-CABLE 비활성화에 실패했습니다. 장치 관리자에서 VB-Audio Virtual Cable을 사용 안 함으로 바꿔 주세요.",
            L"Portable Mixer 장치 종료 실패", MB_OK | MB_ICONERROR | MB_SETFOREGROUND); });
}
inline int guardCommandLine() {
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return -1;
    if (argc < 2 || wcscmp(argv[1], L"--cable-guard")) { LocalFree(argv); return -1; }
    HANDLE handles[4]{}; bool valid = argc == 6;
    for (int i = 0; valid && i < 4; ++i) {
        wchar_t* end = nullptr;
        handles[i] = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(wcstoull(argv[i+2], &end, 10)));
        DWORD flags = 0;
        valid = end && !*end && GetHandleInformation(handles[i], &flags);
    }
    LocalFree(argv);
    return valid ? runGuard(handles[0], handles[1], handles[2], handles[3]) : 2;
}
class Device {
    HANDLE stop_ = nullptr, ready_ = nullptr, mapping_ = nullptr, process_ = nullptr;
    GuardState* state_ = nullptr;
    std::wstring render_, error_;
    bool captureReady_ = false;
    ULONGLONG lastProbe_ = 0;
public:
    Device() {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE parent = OpenProcess(SYNCHRONIZE, TRUE, GetCurrentProcessId());
        stop_ = CreateEventW(&sa, TRUE, FALSE, nullptr);
        ready_ = CreateEventW(&sa, TRUE, FALSE, nullptr);
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(GuardState), nullptr);
        if (mapping_) state_ = static_cast<GuardState*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GuardState)));
        if (!parent || !stop_ || !ready_ || !state_) {
            if (parent) CloseHandle(parent);
            error_ = L"가상 마이크 종료 감시를 준비하지 못했습니다."; return;
        }
        *state_ = {}; state_->result = Pending;
        wchar_t exe[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, exe, 32768);
        HANDLE inherited[]{parent, stop_, ready_, mapping_};
        SIZE_T size = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<BYTE> storage(size);
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        bool initialized = InitializeProcThreadAttributeList(attrs, 1, 0, &size) != FALSE;
        bool configured = initialized && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited, sizeof(inherited), nullptr, nullptr);
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW; startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.lpAttributeList = attrs;
        std::wstring command = L"\"" + std::wstring(exe) + L"\" --cable-guard";
        for (HANDLE h : inherited) command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(h));
        PROCESS_INFORMATION info{};
        if (length && length < 32768 && configured && CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo, &info)) {
            process_ = info.hProcess; CloseHandle(info.hThread);
        } else error_ = L"가상 마이크 종료 감시 프로세스를 시작하지 못했습니다.";
        if (initialized) DeleteProcThreadAttributeList(attrs);
        for (HANDLE h : inherited) SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        CloseHandle(parent);
    }
    ~Device() {
        if (stop_) SetEvent(stop_);
        if (process_) {
            const DWORD stopped = WaitForSingleObject(process_, 5000);
            if (stopped == WAIT_OBJECT_0 &&
                state_ && WaitForSingleObject(ready_, 0) == WAIT_OBJECT_0 && state_->result == CR_SUCCESS) {
                // Recover if the watchdog alone was killed. Do not race a new owner.
                HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Global\\PortableMixer.VBCable.Lease.v1");
                if (mutex) {
                    const DWORD acquired = WaitForSingleObject(mutex, 0);
                    if (acquired == WAIT_OBJECT_0 || acquired == WAIT_ABANDONED) {
                        const auto result = change(state_->instance, false);
                        ReleaseMutex(mutex);
                        if (result != CR_SUCCESS && result != CR_NO_SUCH_DEVNODE)
                            MessageBoxW(nullptr, L"VB-CABLE을 비활성화하지 못했습니다. 장치 관리자에서 사용 안 함으로 바꿔 주세요.",
                                L"Portable Mixer", MB_OK | MB_ICONERROR);
                    }
                    CloseHandle(mutex);
                }
            }
            CloseHandle(process_);
        }
        if (state_) UnmapViewOfFile(state_);
        for (HANDLE h : {stop_, ready_, mapping_}) if (h) CloseHandle(h);
    }
    Device(const Device&) = delete;
    bool ready() {
        if (!error_.empty() || !process_ || WaitForSingleObject(process_, 0) != WAIT_TIMEOUT ||
            WaitForSingleObject(ready_, 0) != WAIT_OBJECT_0 || state_->result != CR_SUCCESS) return false;
        if (GetTickCount64() - lastProbe_ >= 1000) {
            lastProbe_ = GetTickCount64();
            render_ = findEndpoint(state_->instance, eRender);
            captureReady_ = !findEndpoint(state_->instance, eCapture).empty();
        }
        return !render_.empty() && captureReady_;
    }
    const std::wstring& renderId() const { return render_; }
    bool ownsEndpoint(IMMDevice* device, IMMDeviceEnumerator* enumerator) const {
        return state_ && WaitForSingleObject(ready_, 0) == WAIT_OBJECT_0 && state_->instance[0] &&
            endpointBelongsTo(device, enumerator, state_->instance);
    }
    std::wstring status() {
        if (!error_.empty()) return error_;
        if (WaitForSingleObject(ready_, 0) != WAIT_OBJECT_0) return L"VB-CABLE 장치를 활성화하고 있습니다…";
        switch (state_->result) {
        case Missing: return L"VB-CABLE 설치가 필요합니다. 설치 후 앱을 다시 실행하세요.";
        case Busy: return L"다른 Portable Mixer가 VB-CABLE을 사용 중입니다.";
        case Ambiguous: return L"VB-CABLE이 여러 개 감지되어 자동 제어를 중단했습니다.";
        case CR_SUCCESS: break;
        default: return L"VB-CABLE 활성화 실패 (CM " + std::to_wstring(state_->result) + L"). 관리자 권한·장치 상태를 확인하세요.";
        }
        if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT) return L"종료 감시가 중단되었습니다. 앱을 종료 후 다시 실행하세요.";
        return ready() ? L"VB-CABLE 연결됨 · Discord 입력: CABLE Output · 앱 종료 시 비활성화" : L"VB-CABLE 오디오 연결 대기 중. 재부팅·장치 사용 설정을 확인하세요.";
    }
};
}
