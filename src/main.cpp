#include "audio_engine.hpp"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwchar>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT TrayMessage = WM_APP + 1;
enum : int { Pid = 100, Processes, Refresh, Microphones, ProcessGain, MicGain, Start, Stop, Hide, Exit, Show };
struct Mic { std::wstring id, name; };
struct Proc { DWORD pid; std::wstring name; };
std::unique_ptr<AudioEngine> engine;
HWND window{}, pidEdit{}, procCombo{}, micCombo{}, processSlider{}, micSlider{}, processLabel{}, micLabel{}, statusLabel{}, meterLabel{};
HWND startButton{}, stopButton{}, refreshButton{};
std::vector<Proc> processes;
std::vector<Mic> microphones;
NOTIFYICONDATAW tray{};
bool trayAdded = false;
UINT taskbarCreated = 0;
HFONT font{};

HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return c;
}
void refresh() {
    const int oldMic = static_cast<int>(SendMessageW(micCombo, CB_GETCURSEL, 0, 0));
    const std::wstring selectedId = oldMic >= 0 && oldMic < static_cast<int>(microphones.size()) ? microphones[oldMic].id : L"";
    processes.clear(); SendMessageW(procCombo, CB_RESETCONTENT, 0, 0);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W p{}; p.dwSize = sizeof(p);
        if (Process32FirstW(snapshot, &p)) do {
            if (p.th32ProcessID && p.th32ProcessID != GetCurrentProcessId()) processes.push_back({p.th32ProcessID, p.szExeFile});
        } while (Process32NextW(snapshot, &p));
        CloseHandle(snapshot);
    }
    std::sort(processes.begin(), processes.end(), [](const Proc& a, const Proc& b){
        int c = _wcsicmp(a.name.c_str(), b.name.c_str()); return c ? c < 0 : a.pid < b.pid;
    });
    for (const auto& p : processes) {
        const auto label = p.name + L"  [" + std::to_wstring(p.pid) + L"]";
        SendMessageW(procCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    microphones.clear(); SendMessageW(micCombo, CB_RESETCONTENT, 0, 0);
    ComPtr<IMMDeviceEnumerator> enumerator; ComPtr<IMMDeviceCollection> collection;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
        FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection))) return;
    UINT count = 0; collection->GetCount(&count);
    int selected = 0;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device; ComPtr<IPropertyStore> properties;
        if (FAILED(collection->Item(i, &device))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        Mic mic{id, L"Microphone"}; CoTaskMemFree(id);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT name{};
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR && name.pwszVal)
                mic.name = name.pwszVal;
            PropVariantClear(&name);
        }
        if (mic.id == selectedId) selected = static_cast<int>(microphones.size());
        microphones.push_back(std::move(mic));
        SendMessageW(micCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(microphones.back().name.c_str()));
    }
    if (!microphones.empty()) SendMessageW(micCombo, CB_SETCURSEL, selected, 0);
}
void updateGains() {
    const int p = static_cast<int>(SendMessageW(processSlider, TBM_GETPOS, 0, 0));
    const int m = static_cast<int>(SendMessageW(micSlider, TBM_GETPOS, 0, 0));
    engine->processGain = p/100.0f; engine->micGain = m/100.0f;
    SetWindowTextW(processLabel, (L"Process volume: " + std::to_wstring(p) + L"%").c_str());
    SetWindowTextW(micLabel, (L"Microphone volume: " + std::to_wstring(m) + L"%").c_str());
}
void update() {
    bool active = engine->active.load();
    for (HWND c : {pidEdit, procCombo, micCombo, refreshButton, startButton}) EnableWindow(c, !active);
    EnableWindow(stopButton, active);
    SetWindowTextW(statusLabel, engine->statusText().c_str());
    wchar_t meter[400]{};
    swprintf_s(meter, L"Peaks  App %.0f%% | Mic %.0f%% | Mix %.0f%%\nClipped %llu | Late %llu | Empty %llu | Overflow %llu\nDiscontinuities %llu | Timestamp fallbacks %llu",
        engine->processPeak.load()*100, engine->micPeak.load()*100, engine->outputPeak.load()*100,
        static_cast<unsigned long long>(engine->clips.load()), static_cast<unsigned long long>(engine->late.load()),
        static_cast<unsigned long long>(engine->missing.load()), static_cast<unsigned long long>(engine->overflows.load()),
        static_cast<unsigned long long>(engine->discontinuities.load()),
        static_cast<unsigned long long>(engine->timestampErrors.load()));
    SetWindowTextW(meterLabel, meter);
}
void addTray() {
    tray = {}; tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = TrayMessage;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"Portable Mixer - WAV capture (no Discord mic output)");
    trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayAdded) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); }
}
void show() { ShowWindow(window, SW_SHOWNORMAL); SetForegroundWindow(window); }
void trayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Show, L"Open mixer");
    AppendMenuW(menu, MF_STRING | (engine->active.load() ? 0 : MF_GRAYED), Stop, L"Stop recording");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, Exit, L"Exit");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0); DestroyMenu(menu);
}
void begin() {
    wchar_t pidText[40]{}; GetWindowTextW(pidEdit, pidText, 40);
    wchar_t* end = nullptr; const unsigned long long pid = wcstoull(pidText, &end, 10);
    if (!pid || pid > MAXDWORD || !end || *end || pid == GetCurrentProcessId()) {
        MessageBoxW(window, L"Select a target process or enter a valid PID.", L"Target process", MB_ICONINFORMATION); return;
    }
    const int micIndex = static_cast<int>(SendMessageW(micCombo, CB_GETCURSEL, 0, 0));
    if (micIndex < 0 || micIndex >= static_cast<int>(microphones.size())) {
        MessageBoxW(window, L"No active microphone selected. Check Windows microphone permissions and press Refresh.", L"Microphone", MB_ICONINFORMATION); return;
    }
    wchar_t path[32768] = L"mix.wav";
    OPENFILENAMEW save{}; save.lStructSize = sizeof(save); save.hwndOwner = window;
    save.lpstrFilter = L"PCM WAV (*.wav)\0*.wav\0\0"; save.lpstrFile = path; save.nMaxFile = 32768;
    save.lpstrDefExt = L"wav"; save.lpstrTitle = L"Save mixed audio (this does not transmit to Discord)";
    save.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&save)) return;
    try { engine->start(static_cast<DWORD>(pid), microphones[micIndex].id, path); }
    catch (...) { MessageBoxW(window, L"Could not start audio worker.", L"Error", MB_ICONERROR); }
    update();
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated && msg == taskbarCreated) { addTray(); return 0; }
    switch (msg) {
    case WM_CREATE:
        window = hwnd;
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        control(L"STATIC", L"Portable Mixer | capture prototype", 0, 16, 14, 580, 24);
        control(L"STATIC", L"Discord microphone output is NOT available without an installed endpoint/APO.\nThis version captures and mixes to a WAV file only. Speakers are untouched.", 0, 16, 44, 580, 42);
        control(L"STATIC", L"Target process (includes its child processes)", 0, 16, 100, 450, 20);
        procCombo = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 123, 450, 320, Processes);
        refreshButton = control(L"BUTTON", L"Refresh", WS_TABSTOP, 476, 123, 106, 26, Refresh);
        control(L"STATIC", L"PID", 0, 16, 162, 40, 20);
        pidEdit = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 60, 158, 160, 26, Pid);
        control(L"STATIC", L"Microphone (shared capture)", 0, 16, 200, 450, 20);
        micCombo = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 223, 566, 220, Microphones);
        processLabel = control(L"STATIC", L"", 0, 16, 267, 400, 20);
        processSlider = control(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 16, 289, 566, 30, ProcessGain);
        micLabel = control(L"STATIC", L"", 0, 16, 329, 400, 20);
        micSlider = control(TRACKBAR_CLASSW, L"", TBS_HORZ | WS_TABSTOP, 16, 351, 566, 30, MicGain);
        for (HWND slider : {processSlider, micSlider}) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
            SendMessageW(slider, TBM_SETPOS, TRUE, 100);
        }
        startButton = control(L"BUTTON", L"Record WAV...", WS_TABSTOP, 16, 398, 160, 30, Start);
        stopButton = control(L"BUTTON", L"Stop", WS_TABSTOP, 186, 398, 100, 30, Stop);
        control(L"BUTTON", L"Hide to tray", WS_TABSTOP, 296, 398, 140, 30, Hide);
        control(L"BUTTON", L"Exit", WS_TABSTOP, 446, 398, 136, 30, Exit);
        statusLabel = control(L"STATIC", L"", 0, 16, 444, 566, 65);
        meterLabel = control(L"STATIC", L"", 0, 16, 516, 566, 65);
        refresh(); updateGains(); update(); addTray(); SetTimer(hwnd, 1, 150, nullptr);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case Processes:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const int i = static_cast<int>(SendMessageW(procCombo, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < static_cast<int>(processes.size())) SetWindowTextW(pidEdit, std::to_wstring(processes[i].pid).c_str());
            } break;
        case Refresh: refresh(); break;
        case Start: begin(); break;
        case Stop: engine->stop(); update(); break;
        case Hide: if (trayAdded) ShowWindow(hwnd, SW_HIDE); break;
        case Show: show(); break;
        case Exit: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_HSCROLL: updateGains(); return 0;
    case WM_SIZE: if (wParam == SIZE_MINIMIZED && trayAdded) ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_TIMER: update(); return 0;
    case TrayMessage:
        if (LOWORD(lParam) == WM_CONTEXTMENU) trayMenu();
        else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) show();
        return 0;
    case WM_CLOSE: if (trayAdded) ShowWindow(hwnd, SW_HIDE); else DestroyWindow(hwnd); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wParam) { engine->stop(); DestroyWindow(hwnd); } return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1); engine->stop();
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    int code = 1;
    try {
        engine = std::make_unique<AudioEngine>();
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_BAR_CLASSES}; InitCommonControlsEx(&init);
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = instance;
        wc.lpszClassName = L"PortableMixerCapturePrototype"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (RegisterClassW(&wc)) {
            RECT rect{0, 0, 600, 600}; AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
            HWND h = CreateWindowExW(0, wc.lpszClassName, L"Portable Mixer - WAV capture prototype",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                rect.right-rect.left, rect.bottom-rect.top, nullptr, nullptr, instance, nullptr);
            if (h) {
                ShowWindow(h, showCommand); MSG msg{}; BOOL result;
                while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
                    if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                }
                code = result < 0 ? 1 : static_cast<int>(msg.wParam);
            }
        }
    } catch (...) { MessageBoxW(nullptr, L"Portable Mixer could not initialize.", L"Error", MB_ICONERROR); }
    engine.reset(); CoUninitialize(); return code;
}
