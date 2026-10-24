#pragma once
#include <windows.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <algorithm>
#include <string>
#include <vector>
#include "session_mixer.hpp"
#include "app_icons.hpp"
#include "dialog_theme.hpp"

// UI-only dialogs. No capture, network, or encoder ownership lives here.
namespace mixer_dialogs {
struct Program { DWORD pid = 0; std::wstring name; app_icons::Image icon{}; bool playing = false; std::wstring key; bool added=false; };
struct BroadcastSettings { std::wstring file, address, key; };
inline std::wstring value(HWND dialog, int id) {
    HWND item = GetDlgItem(dialog, id);
    int n = GetWindowTextLengthW(item);
    std::wstring text(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(item, text.data(), n + 1); text.resize(n); return text;
}
inline HWND item(HWND parent, const wchar_t* type, const wchar_t* label, DWORD style,
                 int x, int y, int w, int h, int id = 0) {
    RECT r{ui::px(MulDiv(x,14,10)),ui::px(MulDiv(y,14,10)),ui::px(MulDiv(x+w,14,10)),ui::px(MulDiv(y+h,14,10))};
    bool button=wcscmp(type,L"BUTTON")==0;
    if(button) style=(style & ~BS_TYPEMASK)|BS_OWNERDRAW;
    HWND result = CreateWindowExW(0, type, label, WS_VISIBLE | WS_CHILD | style,
        r.left, r.top, r.right-r.left, r.bottom-r.top, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(ui::body), TRUE);
    if(button) SetWindowSubclass(result,dialog_theme::hover,1,0);
    if(wcscmp(type,L"EDIT")==0) SendMessageW(result,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(ui::px(8),ui::px(8)));
    return result;
}
inline INT_PTR modal(HWND owner, DLGPROC proc, void* state, short height) {
    struct Template { DLGTEMPLATE dialog; WORD menu, cls, title; } data{};
    data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    data.dialog.cx = 340; data.dialog.cy = height;
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &data.dialog, owner, proc,
                                  reinterpret_cast<LPARAM>(state));
}
struct Picker { Program selected; std::vector<Program> programs; app_icons::Cache icons; std::vector<std::wstring> excluded; };
inline bool canAdd(HWND h,const Picker& state) {
    auto index=SendDlgItemMessageW(h,10,LB_GETCURSEL,0,0);
    return index>=0 && static_cast<size_t>(index)<state.programs.size() && !state.programs[static_cast<size_t>(index)].added;
}
inline void populate(HWND h, Picker& state) {
    state.programs.clear(); SendDlgItemMessageW(h, 10, LB_RESETCONTENT, 0, 0);
    std::vector<sessions::Channel> channels;
    const HRESULT hr = sessions::enumerate(channels);
    for (const auto& c : channels) {
        if (!c.pid) continue;
        auto key=sessions::applicationKey(c);
        if (std::none_of(state.programs.begin(), state.programs.end(), [&](const Program& p) { return p.key == key; })) {
            bool added=std::find(state.excluded.begin(),state.excluded.end(),key)!=state.excluded.end();
            state.programs.push_back({c.pid,c.name,state.icons.get(c.pid),c.playing,key,added});
        }
    }
    SetDlgItemTextW(h, 12, FAILED(hr) ? L"오디오 세션을 읽지 못했습니다. 오디오 장치를 확인하세요." :
        state.programs.empty() ? L"앱에서 소리를 재생한 뒤 새로고침하세요." : L"오디오가 있는 앱만 표시합니다. 일시정지한 앱도 선택할 수 있어요.");
    std::sort(state.programs.begin(), state.programs.end(), [](const Program& a, const Program& b) {
        int cmp = _wcsicmp(a.name.c_str(), b.name.c_str()); return cmp ? cmp < 0 : a.pid < b.pid;
    });
    for (const auto& program : state.programs) {
        auto label = program.name + L"   (PID " + std::to_wstring(program.pid) + L")";
        auto index = SendDlgItemMessageW(h, 10, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (program.pid == state.selected.pid) SendDlgItemMessageW(h, 10, LB_SETCURSEL, index, 0);
    }
    EnableWindow(GetDlgItem(h, IDOK), canAdd(h,state));
}
inline INT_PTR CALLBACK pickerProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<Picker*>(GetWindowLongPtrW(h, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        state = reinterpret_cast<Picker*>(lp); SetWindowLongPtrW(h, DWLP_USER, lp);
        SetWindowTextW(h, L"프로그램 추가");
        dialog_theme::initialize(h,265);
        item(h, L"STATIC", L"소리를 가져올 실행 중인 프로그램을 선택하세요.", 0, 12, 10, 316, 14);
        item(h, L"STATIC", L"", 0, 12, 27, 316, 24, 12);
        item(h, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, 12, 55, 316, 166, 10);
        SendDlgItemMessageW(h,10,LB_SETITEMHEIGHT,0,ui::px(56));
        item(h, L"BUTTON", L"새로고침", WS_TABSTOP, 12, 231, 70, 22, 11);
        item(h, L"BUTTON", L"추가", WS_TABSTOP | BS_DEFPUSHBUTTON, 176, 231, 70, 22, IDOK);
        item(h, L"BUTTON", L"취소", WS_TABSTOP, 258, 231, 70, 22, IDCANCEL);
        populate(h, *state); return TRUE;
    }
    if(msg==WM_MEASUREITEM) { reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=ui::px(56); return TRUE; }
    if(msg==WM_DRAWITEM && state) {
        auto& d=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d.CtlID==10) {
            ui::fill(d.hDC,d.rcItem,ui::Surface);
            if(d.itemID!=static_cast<UINT>(-1) && d.itemID<state->programs.size()) {
                const auto& program=state->programs[d.itemID];
                RECT r=d.rcItem; InflateRect(&r,-ui::px(4),-ui::px(3));
                bool selected=(d.itemState&ODS_SELECTED)!=0;
                ui::round(d.hDC,r,selected ? RGB(55,48,80) : ui::Surface,8,selected ? ui::Accent : ui::Surface);
                if(program.icon && program.icon->handle) DrawIconEx(d.hDC,r.left+ui::px(10),r.top+ui::px(9),program.icon->handle,ui::px(32),ui::px(32),0,nullptr,DI_NORMAL);
                RECT name{r.left+ui::px(54),r.top+ui::px(6),r.right-ui::px(10),r.top+ui::px(26)};
                ui::text(d.hDC,program.name.c_str(),name,ui::body,ui::Ink,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
                RECT sub{name.left,r.top+ui::px(27),name.right,r.bottom-ui::px(3)};
                auto description=program.added ? std::wstring(L"이미 믹서에 추가됨") : std::wstring(program.playing ? L"재생 세션" : L"일시정지 / 대기")+L"   ·   PID "+std::to_wstring(program.pid);
                ui::text(d.hDC,description.c_str(),sub,ui::small,ui::Muted,DT_SINGLELINE|DT_END_ELLIPSIS);
                if(d.itemState&ODS_FOCUS) { InflateRect(&r,-ui::px(2),-ui::px(2)); DrawFocusRect(d.hDC,&r); }
            }
            return TRUE;
        }
    }
    if (msg == WM_COMMAND && state) {
        int id = LOWORD(wp);
        if (id == 11) { populate(h, *state); return TRUE; }
        if (id == 10 && HIWORD(wp) == LBN_SELCHANGE) {
            EnableWindow(GetDlgItem(h, IDOK), canAdd(h,*state)); return TRUE;
        }
        if (id == IDOK || (id == 10 && HIWORD(wp) == LBN_DBLCLK)) {
            auto index = SendDlgItemMessageW(h, 10, LB_GETCURSEL, 0, 0);
            if (canAdd(h,*state)) {
                state->selected = state->programs[static_cast<size_t>(index)]; EndDialog(h, IDOK);
            }
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
    }
    if (msg == WM_CLOSE) { EndDialog(h, IDCANCEL); return TRUE; }
    return dialog_theme::handle(h,msg,wp,lp);
}
inline bool pickProgram(HWND owner, Program& selected, std::vector<std::wstring> excluded={}) {
    Picker state{selected, {}, {}, std::move(excluded)};
    if (modal(owner, pickerProc, &state, 265) != IDOK) return false;
    selected = std::move(state.selected); return true;
}
inline INT_PTR CALLBACK broadcastProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<BroadcastSettings*>(GetWindowLongPtrW(h, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        state = reinterpret_cast<BroadcastSettings*>(lp); SetWindowLongPtrW(h, DWLP_USER, lp);
        SetWindowTextW(h, L"MP4 방송 송출 설정");
        dialog_theme::initialize(h,270);
        item(h, L"STATIC", L"영상 파일", 0, 12, 12, 310, 14);
        item(h, L"EDIT", state->file.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 12, 30, 232, 20, 20);
        item(h, L"BUTTON", L"찾아보기", WS_TABSTOP, 252, 30, 76, 20, 21);
        item(h, L"STATIC", L"플랫폼에서 제공한 RTMP / RTMPS 서버 주소", 0, 12, 62, 316, 14);
        item(h, L"EDIT", state->address.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 12, 80, 316, 20, 22);
        item(h, L"STATIC", L"스트림 키", 0, 12, 112, 316, 14);
        item(h, L"EDIT", state->key.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, 12, 130, 316, 20, 23);
        item(h, L"STATIC", L"송출 엔진 연결 전입니다. 이 창은 설정만 보관하며 방송을 시작하지 않습니다. 설정은 앱 종료 시 지워집니다.", 0, 12, 165, 316, 38);
        item(h, L"STATIC", L"", 0, 12, 205, 316, 24, 24);
        item(h, L"BUTTON", L"설정 적용", WS_TABSTOP | BS_DEFPUSHBUTTON, 176, 236, 70, 22, IDOK);
        item(h, L"BUTTON", L"취소", WS_TABSTOP, 258, 236, 70, 22, IDCANCEL);
        return TRUE;
    }
    if (msg == WM_COMMAND && state) {
        if (LOWORD(wp) == 21) {
            wchar_t path[32768]{}; OPENFILENAMEW open{}; open.lStructSize = sizeof(open);
            open.hwndOwner = h; open.lpstrFilter = L"MP4 영상 (*.mp4)\0*.mp4\0\0";
            open.lpstrFile = path; open.nMaxFile = 32768;
            open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&open)) SetDlgItemTextW(h, 20, path);
            return TRUE;
        }
        if (LOWORD(wp) == IDOK) {
            auto file = value(h, 20), address = value(h, 22), key = value(h, 23);
            if (file.empty() || GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES ||
                (address.rfind(L"rtmp://", 0) != 0 && address.rfind(L"rtmps://", 0) != 0) || key.empty()) {
                SetDlgItemTextW(h,24,L"MP4 파일, RTMP(S) 서버 주소와 스트림 키를 확인하세요.");
                return TRUE;
            }
            *state = {std::move(file), std::move(address), std::move(key)}; EndDialog(h, IDOK); return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
    }
    if (msg == WM_CLOSE) { EndDialog(h, IDCANCEL); return TRUE; }
    return dialog_theme::handle(h,msg,wp,lp);
}
inline void configureBroadcast(HWND owner, BroadcastSettings& settings) {
    modal(owner, broadcastProc, &settings, 270);
}
} // namespace mixer_dialogs
