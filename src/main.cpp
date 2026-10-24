#include "audio_engine.hpp"
#include "virtual_microphone.hpp"
#include "cable_installer.hpp"
#include "ui_theme.hpp"
#include "brand.hpp"
#include "mixer_dialogs.hpp"
#include "session_panel.hpp"
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
constexpr int AddProgram = 400, BroadcastSetup = 401;
HWND addProgramButton{}, selectedProgramLabel{};
mixer_dialogs::Program selectedProgram;
mixer_dialogs::BroadcastSettings broadcastSettings;
SessionPanel sessionPanel;
HWND sessionPanelWindow{}, backToMixer{};
constexpr UINT TrayMessage = WM_APP + 1;
enum : int { Pid = 100, Processes, Refresh, Microphones, ProcessGain, MicGain, Start, Stop, Hide, Exit, Show, MuteProcess, MuteMic, Details, RecordWav, CableHelp };
struct Mic { std::wstring id, name; };
struct Proc { DWORD pid; std::wstring name; };
std::unique_ptr<AudioEngine> engine;
HWND window{}, pidEdit{}, procCombo{}, micCombo{}, processSlider{}, micSlider{}, processLabel{}, micLabel{}, statusLabel{}, meterLabel{};
HWND startButton{}, stopButton{}, refreshButton{};
HWND processMute{}, micMute{}, detailsButton{};
HWND recordCheck{}, virtualStatus{};
std::unique_ptr<virtual_mic::Device> virtualDevice;
bool detailsVisible = false, wasActive = false;
int savedProcessGain = 100, savedMicGain = 100;
ULONGLONG recordingStart = 0, elapsed = 0;
std::wstring outputPath;
std::vector<Proc> processes;
std::vector<Mic> microphones;
NOTIFYICONDATAW tray{};
bool trayAdded = false;
UINT taskbarCreated = 0;

HWND control(const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0) {
    HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, ui::px(x), ui::px(y), ui::px(w), ui::px(h),
                            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::body), TRUE);
    if(wcscmp(type,L"BUTTON")==0 && (style & BS_TYPEMASK)==BS_OWNERDRAW)
        SetWindowSubclass(c,dialog_theme::hover,1,0);
    if(wcscmp(type,L"BUTTON")==0 && (style & BS_TYPEMASK)==BS_AUTOCHECKBOX) {
        if(HMODULE theme=LoadLibraryW(L"uxtheme.dll")) {
            using SetTheme=HRESULT (WINAPI*)(HWND,LPCWSTR,LPCWSTR);
            auto set=reinterpret_cast<SetTheme>(GetProcAddress(theme,"SetWindowTheme"));
            if(set) set(c,L"",L"");
            FreeLibrary(theme);
        }
    }
    ui::controls.push_back({c, x, y, w, h});
    return c;
}
void showMixer(bool visible) {
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using SetAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttribute = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        BOOL dark = TRUE;
        if (setAttribute) setAttribute(window, 20, &dark, sizeof(dark));
        FreeLibrary(dwm);
    }
    RECT bounds{0, 0, ui::px(visible ? SessionPanel::Width : 800), ui::px(visible ? SessionPanel::Height : 846)};
    AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE, 0, ui::dpi);
    SetWindowPos(window, nullptr, 0, 0, bounds.right-bounds.left, bounds.bottom-bounds.top, SWP_NOMOVE | SWP_NOZORDER);
    for (const auto& c : ui::controls) ShowWindow(c.hwnd, visible ? SW_HIDE : SW_SHOW);
    ShowWindow(procCombo, SW_HIDE); ShowWindow(pidEdit, SW_HIDE);
    ShowWindow(meterLabel, !visible && detailsVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(sessionPanelWindow, visible ? SW_SHOW : SW_HIDE);
    SetWindowTextW(window, visible ? L"Portable Mixer — 프로그램 볼륨 믹서" : L"Portable Mixer — 녹음 및 입력 설정");
    SetFocus(visible ? GetWindow(sessionPanelWindow, GW_CHILD) : backToMixer);
    InvalidateRect(window, nullptr, TRUE);
}
void refresh() {
    wchar_t selectedPid[40]{}; GetWindowTextW(pidEdit, selectedPid, 40);
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
    for (size_t i = 0; i < processes.size(); ++i)
        if (std::to_wstring(processes[i].pid) == selectedPid) SendMessageW(procCombo, CB_SETCURSEL, i, 0);
    SendMessageW(procCombo, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(L"녹음할 앱을 선택하세요"));
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
        if (virtualDevice && virtualDevice->ownsEndpoint(device.Get(), enumerator.Get())) continue;
        if (mic.id == selectedId) selected = static_cast<int>(microphones.size());
        microphones.push_back(std::move(mic));
        SendMessageW(micCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(microphones.back().name.c_str()));
    }
    if (!microphones.empty()) SendMessageW(micCombo, CB_SETCURSEL, selected, 0);
    SendMessageW(micCombo, CB_SETCUEBANNER, 0, reinterpret_cast<LPARAM>(microphones.empty() ? L"연결된 마이크가 없습니다" : L"녹음할 마이크를 선택하세요"));
}
void updateGains() {
    const int p = static_cast<int>(SendMessageW(processSlider, TBM_GETPOS, 0, 0));
    const int m = static_cast<int>(SendMessageW(micSlider, TBM_GETPOS, 0, 0));
    engine->processGain = p/100.0f; engine->micGain = m/100.0f;
    SetWindowTextW(processLabel, (std::to_wstring(p) + L"%").c_str());
    SetWindowTextW(micLabel, (std::to_wstring(m) + L"%").c_str());
    SetWindowTextW(processMute, p ? L"음소거" : L"음소거 해제");
    SetWindowTextW(micMute, m ? L"음소거" : L"음소거 해제");
}
void update() {
    bool active = engine->active.load();
    if (active && (!virtualDevice || !virtualDevice->ready())) { engine->stop(); active = false; }
    EnableWindow(addProgramButton, !active);
    const auto status = engine->statusText();
    const bool recording = active && status.rfind(L"Mixing", 0) == 0;
    if (recording && !wasActive) { recordingStart = GetTickCount64(); elapsed = 0; }
    if (wasActive || recording) elapsed = GetTickCount64() - recordingStart;
    wasActive = recording;
    for (HWND c : {pidEdit, procCombo, micCombo, refreshButton, recordCheck}) EnableWindow(c, !active);
    EnableWindow(startButton, !active && virtualDevice && virtualDevice->ready());
    if (virtualDevice) SetWindowTextW(virtualStatus, virtualDevice->status().c_str());
    EnableWindow(stopButton, active);
    std::wstring friendly = status;
    if (status.rfind(L"Idle.", 0) == 0) friendly = L"앱과 마이크를 선택한 뒤 녹음을 시작하세요.";
    else if (status == L"Starting capture...") friendly = L"오디오 입력을 연결하고 있습니다…";
    else if (status.rfind(L"Mixing", 0) == 0) friendly = status == L"Mixing to virtual microphone and WAV." ? L"가상 마이크 송출 중 · WAV 파일도 저장하고 있습니다." : L"가상 마이크 송출 중 · Discord에서 입력 장치를 선택하세요.";
    else if (status == L"Stopped. WAV finalized.") friendly = L"녹음 완료 · WAV 파일을 저장했습니다.";
    else if (status == L"Stopped.") friendly = L"송출이 중지되었습니다. VB-CABLE은 앱 종료 시 비활성화됩니다.";
    else if (status == L"Target process exited. Mix stopped.") friendly = L"대상 앱이 종료되어 녹음을 마쳤습니다.";
    if (microphones.empty() && !active && status.rfind(L"Idle.", 0) == 0)
        friendly = L"사용할 수 있는 마이크가 없습니다. 마이크를 연결하고 새로고침하세요.";
    int len = GetWindowTextLengthW(statusLabel);
    std::wstring old(static_cast<size_t>(len)+1, L'\0'); GetWindowTextW(statusLabel, old.data(), len+1); old.resize(len);
    if (old != friendly) SetWindowTextW(statusLabel, friendly.c_str());
    wchar_t meter[450]{};
    swprintf_s(meter, L"Peaks  App %.0f%% | Mic %.0f%% | Mix %.0f%%\nClipped %llu | Late %llu | Empty %llu | Overflow %llu\nDiscontinuities %llu | Timestamp fallbacks %llu",
        engine->processPeak.load()*100, engine->micPeak.load()*100, engine->outputPeak.load()*100,
        static_cast<unsigned long long>(engine->clips.load()), static_cast<unsigned long long>(engine->late.load()),
        static_cast<unsigned long long>(engine->missing.load()), static_cast<unsigned long long>(engine->overflows.load()),
        static_cast<unsigned long long>(engine->discontinuities.load()),
        static_cast<unsigned long long>(engine->timestampErrors.load()));
    SetWindowTextW(meterLabel, meter);
    RECT meters = ui::rect(40, 343, 720, 14); InvalidateRect(window, &meters, FALSE);
    RECT output = ui::rect(40, 494, 720, 60); InvalidateRect(window, &output, FALSE);
    RECT badge = ui::rect(640, 24, 132, 44); InvalidateRect(window, &badge, FALSE);
}
void mute(HWND slider, int& saved) {
    int gain = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
    if (gain) saved = gain;
    SendMessageW(slider, TBM_SETPOS, TRUE, gain ? 0 : saved);
    updateGains();
}
void addTray() {
    tray = {}; tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = TrayMessage;
    tray.hIcon = brand::icon(GetSystemMetrics(SM_CXSMICON));
    wcscpy_s(tray.szTip, L"Portable Mixer · 앱 오디오 + 마이크 믹스");
    trayAdded = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayAdded) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); }
}
void show() { ShowWindow(window, SW_SHOWNORMAL); SetForegroundWindow(window); }
void trayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Show, L"믹서 열기");
    AppendMenuW(menu, MF_STRING | (engine->active.load() ? 0 : MF_GRAYED), Stop, L"녹음 중지");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, Exit, L"종료");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0); DestroyMenu(menu);
}
void begin() {
    wchar_t pidText[40]{}; GetWindowTextW(pidEdit, pidText, 40);
    wchar_t* end = nullptr; const unsigned long long pid = wcstoull(pidText, &end, 10);
    if (!pid || pid > MAXDWORD || !end || *end || pid == GetCurrentProcessId()) {
        MessageBoxW(window, L"프로그램 추가를 눌러 녹음할 앱을 선택하세요.", L"앱 선택", MB_ICONINFORMATION); SetFocus(addProgramButton); return;
    }
    const int micIndex = static_cast<int>(SendMessageW(micCombo, CB_GETCURSEL, 0, 0));
    if (micIndex < 0 || micIndex >= static_cast<int>(microphones.size())) {
        MessageBoxW(window, L"마이크를 연결하고 Windows 마이크 접근 권한을 확인한 뒤 새로고침하세요.", L"마이크 선택", MB_ICONINFORMATION); SetFocus(micCombo); return;
    }
    if (!virtualDevice || !virtualDevice->ready()) {
        MessageBoxW(window, L"VB-CABLE을 설치한 뒤 앱을 다시 실행하세요. 장치 상태는 아래 안내를 확인하세요.", L"가상 마이크", MB_ICONINFORMATION); return;
    }
    const bool saveWav = SendMessageW(recordCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    wchar_t path[32768] = L"mix.wav";
    OPENFILENAMEW save{}; save.lStructSize = sizeof(save); save.hwndOwner = window;
    save.lpstrFilter = L"PCM WAV (*.wav)\0*.wav\0\0"; save.lpstrFile = path; save.nMaxFile = 32768;
    save.lpstrDefExt = L"wav"; save.lpstrTitle = L"녹음할 WAV 파일 위치 선택";
    save.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (saveWav && !GetSaveFileNameW(&save)) return;
    if (!saveWav) path[0] = 0;
    try {
        engine->start(static_cast<DWORD>(pid), microphones[micIndex].id, path, virtualDevice->renderId());
        outputPath = path; elapsed = 0;
        InvalidateRect(window, nullptr, FALSE);
    }
    catch (...) { MessageBoxW(window, L"Could not start audio worker.", L"Error", MB_ICONERROR); }
    update();
}
void paint(HDC dc) {
    using namespace ui;
    RECT client{}; GetClientRect(window, &client); fill(dc, client, Background);
    brand::draw(dc, px(28), px(28), px(46));
    text(dc, L"Portable Mixer", rect(88, 24, 440, 36), title);
    text(dc, L"앱 오디오와 내 목소리를 하나로 믹스합니다.", rect(89, 65, 500, 24), body, Muted);
    const bool active = engine->active.load();
    round(dc, rect(650, 32, 122, 30), active ? RGB(255, 235, 239) : Soft, 24, Background);
    const bool starting = active && engine->statusText() == L"Starting capture...";
    text(dc, starting ? L"●  연결 중" : active ? L"●  송출 중" : L"●  준비", rect(650, 32, 122, 30), body,
        active ? RGB(196, 53, 80) : Accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    for (int x : {28, 410}) round(dc, rect(x, 116, 362, 268), Surface, 20, Line);
    text(dc, L"01  프로그램", rect(48, 134, 300, 28), heading);
    text(dc, L"녹음 입력 1개 · 선택한 앱과 하위 프로세스", rect(48, 165, 320, 22), small, Muted);
    text(dc, L"02  마이크", rect(430, 134, 300, 28), heading);
    text(dc, L"함께 녹음할 입력 장치", rect(430, 165, 310, 22), small, Muted);
    text(dc, L"볼륨", rect(48, 270, 80, 22), body, Muted);
    text(dc, L"볼륨", rect(430, 270, 80, 22), body, Muted);
    text(dc, L"0", rect(48, 357, 30, 18), small, Muted);
    text(dc, L"200%", rect(320, 357, 50, 18), small, Muted, DT_RIGHT | DT_SINGLELINE);
    text(dc, L"0", rect(430, 357, 30, 18), small, Muted);
    text(dc, L"200%", rect(702, 357, 50, 18), small, Muted, DT_RIGHT | DT_SINGLELINE);
    meter(dc, 48, 346, 322, active ? engine->processPeak.load() : 0, Accent);
    meter(dc, 430, 346, 322, active ? engine->micPeak.load() : 0, Green);
    round(dc, rect(28, 400, 744, 270), Surface, 20, Line);
    text(dc, L"믹스 출력", rect(48, 416, 250, 28), heading);
    text(dc, L"VB-CABLE / 48 kHz / Stereo", rect(470, 418, 282, 24), small, Muted, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    wchar_t time[32]{}; auto seconds = elapsed / 1000;
    swprintf_s(time, L"%02llu:%02llu:%02llu", seconds/3600, (seconds/60)%60, seconds%60);
    text(dc, time, rect(48, 496, 220, 44), number);
    text(dc, engine->clips.load() ? L"출력 레벨 · 음량이 높습니다. 볼륨을 낮추세요." : L"출력 레벨", rect(300, 500, 452, 22), small,
        engine->clips.load() ? RGB(196, 53, 80) : Muted);
    meter(dc, 300, 533, 452, active ? engine->outputPeak.load() : 0, Accent);
    text(dc, outputPath.empty() ? L"가상 마이크로 실시간 송출합니다. WAV 저장은 선택 사항입니다." : outputPath.c_str(), rect(48, 627, 704, 24), small, Muted,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
    text(dc, L"창 닫기·종료 시 가상 장치 제거 · 최소화·트레이 숨김 중에는 유지", rect(28, 742, 744, 24), small, Muted);
}
void drawButton(const DRAWITEMSTRUCT& item) {
    using namespace ui;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = item.CtlID == Start;
    COLORREF bg = disabled ? Background : primary ? Accent : Soft;
    if (pressed && !disabled) bg = primary ? RGB(80, 58, 190) : RGB(222, 216, 250);
    fill(item.hDC, item.rcItem, Surface);
    round(item.hDC, item.rcItem, bg, 10, bg);
    wchar_t label[100]{}; GetWindowTextW(item.hwndItem, label, 100);
    text(item.hDC, label, item.rcItem, body, disabled ? Muted : primary ? Surface : Accent, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) { RECT focus = item.rcItem; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(item.hDC, &focus); }
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated && msg == taskbarCreated) { addTray(); return 0; }
    switch (msg) {
    case WM_CREATE:
        window = hwnd;
        ui::dpi = GetDpiForWindow(hwnd); ui::fonts();
        procCombo = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 48, 198, 322, 320, Processes);
        pidEdit = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, 150, 232, 102, 26, Pid);
        SendMessageW(pidEdit, EM_SETLIMITTEXT, 10, 0);
        ShowWindow(procCombo, SW_HIDE); ShowWindow(pidEdit, SW_HIDE);
        selectedProgramLabel = control(L"STATIC", L"추가된 프로그램 없음", SS_LEFT | SS_ENDELLIPSIS, 48, 198, 322, 27);
        addProgramButton = control(L"BUTTON", L"+ 프로그램 추가", BS_OWNERDRAW | WS_TABSTOP, 48, 232, 176, 28, AddProgram);
        control(L"BUTTON", L"MP4 방송 설정", BS_OWNERDRAW | WS_TABSTOP, 238, 232, 132, 28, BroadcastSetup);
        micCombo = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 430, 198, 322, 240, Microphones);
        refreshButton = control(L"BUTTON", L"새로고침", BS_OWNERDRAW | WS_TABSTOP, 652, 232, 100, 28, Refresh);
        processLabel = control(L"STATIC", L"100%", SS_RIGHT, 270, 270, 100, 22);
        micLabel = control(L"STATIC", L"100%", SS_RIGHT, 652, 270, 100, 22);
        processSlider = control(TRACKBAR_CLASSW, L"앱 오디오 볼륨", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 48, 300, 210, 30, ProcessGain);
        micSlider = control(TRACKBAR_CLASSW, L"마이크 볼륨", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 430, 300, 210, 30, MicGain);
        processMute = control(L"BUTTON", L"음소거", BS_OWNERDRAW | WS_TABSTOP, 266, 300, 104, 30, MuteProcess);
        micMute = control(L"BUTTON", L"음소거", BS_OWNERDRAW | WS_TABSTOP, 648, 300, 104, 30, MuteMic);
        for (HWND slider : {processSlider, micSlider}) {
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
            SendMessageW(slider, TBM_SETPOS, TRUE, 100);
            SendMessageW(slider, TBM_SETPAGESIZE, 0, 10);
        }
        statusLabel = control(L"STATIC", L"", SS_LEFT, 48, 450, 704, 42);
        startButton = control(L"BUTTON", L"●  믹스 시작", BS_OWNERDRAW | WS_TABSTOP, 48, 566, 200, 44, Start);
        stopButton = control(L"BUTTON", L"■  송출 중지", BS_OWNERDRAW | WS_TABSTOP, 260, 566, 156, 44, Stop);
        control(L"BUTTON", L"트레이로 숨기기", BS_OWNERDRAW | WS_TABSTOP, 428, 566, 158, 44, Hide);
        control(L"BUTTON", L"종료", BS_OWNERDRAW | WS_TABSTOP, 598, 566, 154, 44, Exit);
        recordCheck = control(L"BUTTON", L"WAV 파일에도 저장", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 682, 240, 30, RecordWav);
        virtualStatus = control(L"STATIC", L"가상 마이크 준비 중…", SS_LEFT, 28, 716, 744, 24);
        control(L"BUTTON", L"VB-CABLE 설치 / 안내", BS_OWNERDRAW | WS_TABSTOP, 28, 744, 220, 26, CableHelp);
        detailsButton = control(L"BUTTON", L"진단 정보 보기", BS_OWNERDRAW | WS_TABSTOP, 28, 778, 132, 30, Details);
        meterLabel = control(L"STATIC", L"", 0, 176, 774, 596, 62);
        ShowWindow(meterLabel, SW_HIDE);
        backToMixer = control(L"BUTTON", L"← 볼륨 믹서", BS_OWNERDRAW | WS_TABSTOP, 640, 76, 132, 30, 902);
        refresh(); updateGains(); update(); addTray(); SetTimer(hwnd, 1, 100, nullptr);
        sessionPanelWindow = sessionPanel.create(hwnd,engine->output);
        if (sessionPanelWindow) showMixer(true);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 900: showMixer(false); break;
        case 901: mixer_dialogs::configureBroadcast(hwnd, broadcastSettings); break;
        case 902: showMixer(true); break;
        case AddProgram:
            if (!engine->active.load() && mixer_dialogs::pickProgram(hwnd, selectedProgram)) {
                SetWindowTextW(pidEdit, std::to_wstring(selectedProgram.pid).c_str());
                SetWindowTextW(selectedProgramLabel, (selectedProgram.name + L" · PID " + std::to_wstring(selectedProgram.pid)).c_str());
                SetWindowTextW(addProgramButton, L"프로그램 변경");
                refresh(); update();
            }
            break;
        case BroadcastSetup: mixer_dialogs::configureBroadcast(hwnd, broadcastSettings); break;
        case Processes:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                const int i = static_cast<int>(SendMessageW(procCombo, CB_GETCURSEL, 0, 0));
                if (i >= 0 && i < static_cast<int>(processes.size())) SetWindowTextW(pidEdit, std::to_wstring(processes[i].pid).c_str());
            } break;
        case Refresh: refresh(); update(); break;
        case CableHelp:
            if (virtual_mic::cables().empty()) {
                if (cable_installer::offer(hwnd)) DestroyWindow(hwnd);
            } else ShellExecuteW(hwnd, L"open", L"https://vb-audio.com/Cable/", nullptr, nullptr, SW_SHOWNORMAL);
            break;

        case MuteProcess: mute(processSlider, savedProcessGain); break;
        case MuteMic: mute(micSlider, savedMicGain); break;
        case Details:
            detailsVisible = !detailsVisible;
            ShowWindow(meterLabel, detailsVisible ? SW_SHOW : SW_HIDE);
            SetWindowTextW(detailsButton, detailsVisible ? L"진단 정보 숨기기" : L"진단 정보 보기");
            break;
        case Start: begin(); break;
        case Stop: engine->stop(); update(); break;
        case Hide: if (trayAdded) ShowWindow(hwnd, SW_HIDE); break;
        case Show: show(); break;
        case Exit: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_HSCROLL: updateGains(); return 0;
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lParam)->itemHeight=ui::px(22); return TRUE;
    case WM_DRAWITEM: {
        auto& d=*reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if(d.CtlType==ODT_COMBOBOX) {
            ui::fill(d.hDC,d.rcItem,(d.itemState&ODS_SELECTED) ? ui::Soft : ui::Surface);
            std::wstring name=L"마이크를 선택하세요";
            if(d.itemID<microphones.size()) name=microphones[d.itemID].name;
            RECT r=d.rcItem; r.left+=ui::px(8); r.right-=ui::px(5);
            ui::text(d.hDC,name.c_str(),r,ui::body,ui::Ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
            if(d.itemState&ODS_FOCUS) DrawFocusRect(d.hDC,&d.rcItem);
        } else drawButton(d);
        return TRUE;
    }
    case WM_NOTIFY: {
        auto* n=reinterpret_cast<NMCUSTOMDRAW*>(lParam);
        if(n->hdr.code==NM_CUSTOMDRAW && (n->hdr.hwndFrom==processSlider || n->hdr.hwndFrom==micSlider)) {
            if(n->dwDrawStage!=CDDS_PREPAINT) return CDRF_DODEFAULT;
            RECT r{},thumb{}; GetClientRect(n->hdr.hwndFrom,&r);
            SendMessageW(n->hdr.hwndFrom,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
            ui::fill(n->hdc,r,ui::Surface);
            int y=(thumb.top+thumb.bottom)/2, x=(thumb.left+thumb.right)/2;
            RECT track{ui::px(10),y-ui::px(2),r.right-ui::px(10),y+ui::px(2)};
            ui::round(n->hdc,track,ui::Line,4,ui::Line);
            track.right=x; ui::round(n->hdc,track,ui::Accent,4,ui::Accent);
            RECT knob{x-ui::px(5),y-ui::px(9),x+ui::px(5),y+ui::px(9)};
            ui::round(n->hdc,knob,ui::Ink,5,ui::Ink);
            if(GetFocus()==n->hdr.hwndFrom) DrawFocusRect(n->hdc,&r);
            return CDRF_SKIPDEFAULT;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, ui::Ink); SetBkColor(dc, ui::Surface);
        return reinterpret_cast<LRESULT>(ui::surface);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT r{}; GetClientRect(hwnd, &r); HDC buffer = CreateCompatibleDC(dc);
        HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
        auto old = SelectObject(buffer, bitmap); paint(buffer);
        BitBlt(dc, 0, 0, r.right, r.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(hwnd, &ps); return 0;
    }
    case WM_DPICHANGED: {
        ui::dpi = HIWORD(wParam); ui::fonts(); ui::layout();
        if (sessionPanelWindow) sessionPanel.dpiChanged();
        auto r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE); return 0;
    }
    case WM_SIZE: if (wParam == SIZE_MINIMIZED && trayAdded) ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_TIMER: update(); return 0;
    case TrayMessage:
        if (LOWORD(lParam) == WM_CONTEXTMENU) trayMenu();
        else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) show();
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wParam) { engine->stop(); DestroyWindow(hwnd); } return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1); engine->stop(); virtualDevice.reset();
        if (trayAdded) Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const int guard = virtual_mic::guardCommandLine();
    if (guard >= 0) return guard;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    int code = 1;
    try {
        if (virtual_mic::cables().empty() && cable_installer::offer(nullptr)) { CoUninitialize(); return 0; }
        virtualDevice = std::make_unique<virtual_mic::Device>();
        engine = std::make_unique<AudioEngine>();
        INITCOMMONCONTROLSEX init{sizeof(init), ICC_BAR_CLASSES}; InitCommonControlsEx(&init);
        taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = wndProc; wc.hInstance = instance;
        wc.lpszClassName = L"PortableMixerCapturePrototype"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = brand::icon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = brand::icon(GetSystemMetrics(SM_CXSMICON));
        if (RegisterClassExW(&wc)) {
            ui::dpi = GetDpiForSystem();
            RECT rect{0, 0, ui::px(SessionPanel::Width), ui::px(SessionPanel::Height)}; AdjustWindowRectExForDpi(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, ui::dpi);
            HWND h = CreateWindowExW(0, wc.lpszClassName, L"Portable Mixer",
                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
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
    engine.reset(); virtualDevice.reset(); ui::release(); CoUninitialize(); return code;
}
