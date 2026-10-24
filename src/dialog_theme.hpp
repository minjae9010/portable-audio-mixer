#pragma once
#include "ui_theme.hpp"
#include <commctrl.h>

namespace dialog_theme {
struct Brushes {
    HBRUSH background = CreateSolidBrush(ui::Background);
    ~Brushes() { DeleteObject(background); }
};
inline Brushes brushes;
inline void initialize(HWND h, int height) {
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
        using SetAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto set = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        BOOL enabled = TRUE;
        if (set) set(h, 20, &enabled, sizeof(enabled));
        FreeLibrary(dwm);
    }
    RECT r{0, 0, ui::px(480), ui::px(MulDiv(height, 14, 10))};
    AdjustWindowRectExForDpi(&r, static_cast<DWORD>(GetWindowLongPtrW(h, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(h, GWL_EXSTYLE)), ui::dpi);
    HWND owner = GetAncestor(GetWindow(h, GW_OWNER), GA_ROOT);
    RECT parent{}; GetWindowRect(owner, &parent);
    MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    int width = r.right-r.left, tall = r.bottom-r.top;
    int x = parent.left+(parent.right-parent.left-width)/2, y=parent.top+(parent.bottom-parent.top-tall)/2;
    x=std::max<int>(monitor.rcWork.left,std::min<int>(x,monitor.rcWork.right-width));
    y=std::max<int>(monitor.rcWork.top,std::min<int>(y,monitor.rcWork.bottom-tall));
    SetWindowPos(h,nullptr,x,y,width,tall,SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(h, DM_SETDEFID, IDOK, 0);
}
inline LRESULT CALLBACK hover(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if(m==BM_SETSTYLE) return DefSubclassProc(h,m,(w & ~BS_TYPEMASK)|BS_OWNERDRAW,l);
    if(m==WM_MOUSEMOVE) {
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0}; TrackMouseEvent(&track); InvalidateRect(h,nullptr,FALSE);
    } else if(m==WM_MOUSELEAVE) InvalidateRect(h,nullptr,FALSE);
    return DefSubclassProc(h,m,w,l);
}
inline INT_PTR handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND: { RECT r{}; GetClientRect(h,&r); ui::fill(reinterpret_cast<HDC>(wp),r,ui::Background); return TRUE; }
    case WM_CTLCOLORDLG: return reinterpret_cast<INT_PTR>(brushes.background);
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wp);
        wchar_t type[32]{}; GetClassNameW(reinterpret_cast<HWND>(lp),type,32);
        bool field=wcscmp(type,L"Edit")==0 || msg==WM_CTLCOLORLISTBOX;
        SetTextColor(dc,ui::Ink); SetBkColor(dc,field ? ui::Surface : ui::Background);
        return reinterpret_cast<INT_PTR>(field ? ui::surface : brushes.background);
    }
    case WM_DRAWITEM: {
        auto& d=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if(d.CtlType!=ODT_BUTTON) return FALSE;
        bool enabled=!(d.itemState&ODS_DISABLED), primary=d.CtlID==IDOK;
        POINT p{}; GetCursorPos(&p); ScreenToClient(d.hwndItem,&p);
        bool hovered=enabled && PtInRect(&d.rcItem,p);
        auto bg=!enabled ? ui::Surface : primary ? ui::Accent : hovered ? RGB(52,59,77) : ui::Soft;
        if(d.itemState&ODS_SELECTED) bg=RGB(78,70,105);
        ui::fill(d.hDC,d.rcItem,ui::Background); ui::round(d.hDC,d.rcItem,bg,10,bg);
        wchar_t label[80]{}; GetWindowTextW(d.hwndItem,label,80);
        ui::text(d.hDC,label,d.rcItem,ui::body,!enabled ? ui::Muted : primary ? ui::Background : ui::Ink,
            DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        if(d.itemState&ODS_FOCUS) { RECT r=d.rcItem; InflateRect(&r,-ui::px(3),-ui::px(3)); DrawFocusRect(d.hDC,&r); }
        return TRUE;
    }
    }
    return FALSE;
}
} // namespace dialog_theme
