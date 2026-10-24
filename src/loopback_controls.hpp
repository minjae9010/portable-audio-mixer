#pragma once
#include "loopback_monitor.hpp"
#include "session_mixer.hpp"
#include "ui_theme.hpp"

class LoopbackControls {
    LoopbackMonitor monitor_;
    HWND source_{}, destination_{}, toggle_{}, status_{};
    struct Endpoint { std::wstring id, name; };
    std::vector<Endpoint> endpoints_;
    std::wstring selected(HWND combo) const {
        auto i = static_cast<size_t>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        return i < endpoints_.size() ? endpoints_[i].id : L"";
    }
public:
    void stop() { monitor_.stop(); }
    void refresh() {
        if (monitor_.active.load()) return;
        const auto oldSource = selected(source_), oldDestination = selected(destination_);
        endpoints_.clear();
        for (HWND combo : {source_, destination_}) SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        sessions::ComPtr<IMMDeviceEnumerator> enumerator;
        sessions::ComPtr<IMMDeviceCollection> devices;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
            FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices))) return;
        std::wstring defaultId, cableId;
        sessions::ComPtr<IMMDevice> def;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &def))) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(def->GetId(&id))) { defaultId = id; CoTaskMemFree(id); }
        }
        UINT count = 0; devices->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            sessions::ComPtr<IMMDevice> device; sessions::ComPtr<IPropertyStore> props;
            if (FAILED(devices->Item(i, &device))) continue;
            LPWSTR id = nullptr; if (FAILED(device->GetId(&id))) continue;
            Endpoint e{id, L"출력 장치"}; CoTaskMemFree(id);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT value{};
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
                    e.name = value.pwszVal;
                PropVariantClear(&value);
            }
            if (e.name.find(L"CABLE Input") != std::wstring::npos) cableId = e.id;
            endpoints_.push_back(std::move(e));
        }
        const auto source = oldSource.empty() ? (cableId.empty() ? defaultId : cableId) : oldSource;
        const auto destination = oldDestination.empty() && source != defaultId ? defaultId : oldDestination;
        populate(source, destination);
    }
    void populate(const std::wstring& source, const std::wstring& destination) {
        for (size_t i = 0; i < endpoints_.size(); ++i) {
            for (HWND combo : {source_, destination_})
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(endpoints_[i].name.c_str()));
            if (endpoints_[i].id == source) SendMessageW(source_, CB_SETCURSEL, i, 0);
            if (endpoints_[i].id == destination) SendMessageW(destination_, CB_SETCURSEL, i, 0);
        }
        SendMessageW(source_, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"확인할 출력 장치 선택"));
        SendMessageW(destination_, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"다른 헤드폰/스피커 선택"));
    }
    void create(HWND parent) {
        // Preserve selections while replacing HWNDs during DPI changes.
        auto source = sourceId_, destination = destinationId_;
        auto make = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
            HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                ui::px(x), ui::px(y), ui::px(w), ui::px(h), parent,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(ui::body), TRUE);
            return child;
        };
        toggle_ = make(L"BUTTON", L"▶ 루프백 듣기", BS_OWNERDRAW, 460, 531, 160, 30, 70);
        source_ = make(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 116, 571, 504, 220, 71);
        destination_ = make(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 116, 609, 504, 220, 72);
        status_ = make(L"STATIC", L"", SS_LEFT, 20, 648, 600, 24, 73);
        if (endpoints_.empty()) refresh(); else populate(source, destination);
        sync();
    }
    void sync() {
        sourceId_ = selected(source_); destinationId_ = selected(destination_);
        const bool active = monitor_.active.load();
        EnableWindow(source_, !active); EnableWindow(destination_, !active);
        const auto text = monitor_.statusText();
        wchar_t old[512]{}; GetWindowTextW(status_, old, 512);
        if (text != old) SetWindowTextW(status_, text.c_str());
        const wchar_t* label = active ? L"■ 듣기 중지" : L"▶ 루프백 듣기";
        GetWindowTextW(toggle_, old, 512);
        if (wcscmp(label, old)) SetWindowTextW(toggle_, label);
    }
    void toggle() {
        if (monitor_.active.load()) monitor_.stop();
        else { try { monitor_.start(selected(source_), selected(destination_)); } catch (...) {} }
        sync();
    }
private:
    std::wstring sourceId_, destinationId_;
};
