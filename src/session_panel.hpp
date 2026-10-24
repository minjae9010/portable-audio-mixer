#pragma once
#include "session_mixer.hpp"
#include "mixer_dialogs.hpp"
#include "ui_theme.hpp"
#include "app_icons.hpp"
#include "brand.hpp"
#include "loopback_controls.hpp"
#include "added_programs.hpp"
#include <commctrl.h>
#include <array>
#include <cwctype>

// Windows session-volume controls are independent of the recording mixer.
class SessionPanel {
public:
    static constexpr int Width = 640, Height = 706;
private:
    static constexpr COLORREF Canvas = RGB(19, 22, 30), Card = RGB(29, 33, 44);
    static constexpr COLORREF Border = RGB(43, 49, 64), Text = RGB(236, 239, 247), Dim = RGB(139, 149, 171);
    static constexpr COLORREF Purple = RGB(176, 157, 255);
    HBRUSH cardBrush_ = CreateSolidBrush(Card);
    HFONT titleFont_{}, valueFont_{}, nameFont_{};
    HWND hwnd_{};
    std::vector<sessions::Channel> channels_;
    sessions::Channel master_;
    static constexpr size_t AppSlots=3;
    sessions::Channel* channelAt(size_t slot) {
        if(slot==AppSlots) return &master_;
        const size_t index=page_*AppSlots+slot;
        return index<channels_.size() ? &channels_[index] : nullptr;
    }
    sessions::AddedPrograms added_;
    std::map<std::wstring,app_icons::Image> selectedIcons_;
    app_icons::Cache iconCache_;
    std::vector<app_icons::Image> icons_;
    struct Strip { HWND slider{}, mute{}, label{}, remove{}; };
    std::array<Strip, 4> strips_{};
    HWND previous_{}, next_{};
    LoopbackControls loopback_;
    size_t page_ = 0;
    unsigned tick_ = 0;
    bool dragging_ = false;
    std::wstring error_;
    static constexpr int Record = 900, Broadcast = 901;
    static COLORREF color(size_t i) {
        constexpr COLORREF colors[]{RGB(176,157,255), RGB(108,215,195), RGB(245,187,121), RGB(133,184,255)};
        return colors[i % 4];
    }
    void fonts() {
        for (auto f : {titleFont_, valueFont_, nameFont_}) if (f) DeleteObject(f);
        titleFont_ = ui::makeFont(22, FW_BOLD, L"Segoe UI");
        valueFont_ = ui::makeFont(23, FW_SEMIBOLD, L"Segoe UI");
        nameFont_ = ui::makeFont(13, FW_BOLD);
    }
    static LRESULT CALLBACK hoverProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
        if (m == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0}; TrackMouseEvent(&track);
            InvalidateRect(h, nullptr, FALSE);
        } else if (m == WM_MOUSELEAVE || m == WM_SETFOCUS || m == WM_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);
        return DefSubclassProc(h, m, w, l);
    }
    HWND make(const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, ui::px(x), ui::px(y), ui::px(w), ui::px(h),
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(ui::body), TRUE);
        if (wcscmp(type, L"BUTTON") == 0) {
            SetWindowLongPtrW(c, GWL_STYLE, GetWindowLongPtrW(c, GWL_STYLE) | BS_OWNERDRAW);
            SetWindowSubclass(c, hoverProc, 1, 0);
        }
        return c;
    }
    void controls() {
        make(L"BUTTON", L"+ 앱 추가", WS_TABSTOP, 20, 84, 112, 32, 1);
        make(L"BUTTON", L"새로고침", WS_TABSTOP, 142, 84, 86, 32, 2);
        make(L"BUTTON", L"녹음 설정", WS_TABSTOP, 420, 84, 96, 32, Record);
        make(L"BUTTON", L"방송 설정", WS_TABSTOP, 526, 84, 94, 32, Broadcast);
        for (int i = 0; i < 4; ++i) {
            const int x = 20 + i * 153;
            strips_[i].label = make(L"STATIC", L"", SS_CENTER, x+12, 238, 117, 30, 20+i);
            SendMessageW(strips_[i].label, WM_SETFONT, reinterpret_cast<WPARAM>(valueFont_), TRUE);
            strips_[i].slider = make(TRACKBAR_CLASSW, L"프로그램 볼륨", TBS_VERT | TBS_NOTICKS | WS_TABSTOP,
                x+43, 278, 44, 116, 30+i);
            SendMessageW(strips_[i].slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(strips_[i].slider, TBM_SETPAGESIZE, 0, 5);
            strips_[i].mute = make(L"BUTTON", L"음소거", WS_TABSTOP, x+17, 404, 107, 28, 40+i);
            strips_[i].remove = make(L"BUTTON", L"×", WS_TABSTOP, x+114, 140, 20, 20, 50+i);
        }
        previous_ = make(L"BUTTON", L"‹", WS_TABSTOP, 526, 460, 40, 28, 3);
        next_ = make(L"BUTTON", L"›", WS_TABSTOP, 580, 460, 40, 28, 4);
        loopback_.create(hwnd_);
    }
    void refresh() {
        std::vector<sessions::Channel> next;
        HRESULT hr = sessions::enumerate(next);
        error_ = FAILED(hr) ? L"오디오 장치를 읽지 못했습니다. 장치 연결과 Windows 오디오 서비스를 확인하세요." : L"";
        // Preserve the first visible channel across additions/removals when possible.
        std::wstring anchor = page_*AppSlots < channels_.size() ? channels_[page_*AppSlots].id : L"";
        channels_ = added_.reconcile(next);
        icons_.clear();
        for (const auto& channel : channels_) {
            auto& icon=selectedIcons_[channel.id];
            if(!channel.volumes.empty()) icon=iconCache_.get(channel.pid);
            icons_.push_back(icon);
        }
        auto it = std::find_if(channels_.begin(), channels_.end(), [&](const auto& c) { return c.id == anchor; });
        if (it != channels_.end()) page_ = static_cast<size_t>(it-channels_.begin())/AppSlots;
        page_ = std::min(page_, channels_.empty() ? size_t(0) : (channels_.size()-1)/AppSlots);
        sync(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void sync() {
        for (size_t i = 0; i < strips_.size(); ++i) {
            auto& s = strips_[i]; auto* channel=channelAt(i);
            bool exists = channel!=nullptr;
            for (HWND h : {s.slider, s.mute, s.label, s.remove}) ShowWindow(h, exists ? SW_SHOW : SW_HIDE);
            if (!exists) continue;
            auto& c = *channel; c.read();
            ShowWindow(s.remove,c.master ? SW_HIDE : SW_SHOW);
            bool online=c.online();
            ShowWindow(s.slider,online ? SW_SHOW : SW_HIDE);
            EnableWindow(s.mute,online);
            int level = static_cast<int>(std::lround(c.level*100));
            if (!dragging_) SendMessageW(s.slider, TBM_SETPOS, TRUE, 100-level);
            auto label = online ? std::to_wstring(level) + L"%" : std::wstring(L"대기");
            wchar_t previous[80]{}; GetWindowTextW(s.label, previous, 80);
            if (label != previous) SetWindowTextW(s.label, label.c_str());
            const wchar_t* muteLabel = c.muted ? L"음소거 해제" : L"음소거";
            GetWindowTextW(s.mute, previous, 80);
            if (wcscmp(previous, muteLabel)) SetWindowTextW(s.mute, muteLabel);
            SetWindowTextW(s.slider, (c.name + L" 볼륨").c_str());
            SetWindowTextW(s.remove,(c.name+L" 제거 ×").c_str());
        }
        EnableWindow(previous_, page_ > 0); EnableWindow(next_, (page_+1)*AppSlots < channels_.size());
    }
    void drawButton(const DRAWITEMSTRUCT& d) {
        using namespace ui;
        bool disabled = (d.itemState & ODS_DISABLED) != 0;
        bool mute = d.CtlID >= 40 && d.CtlID < 44;
        bool remove = d.CtlID >= 50 && d.CtlID < 54;
        auto* channel=mute ? channelAt(d.CtlID-40) : nullptr;
        bool muted = channel && channel->muted;
        POINT p{}; GetCursorPos(&p); ScreenToClient(d.hwndItem, &p);
        bool hover = PtInRect(&d.rcItem, p) && !disabled;
        COLORREF bg = d.CtlID == 1 ? Purple : muted ? RGB(69,49,69) : hover ? RGB(52,59,77) : remove ? Card : RGB(37,43,57);
        if (d.itemState & ODS_SELECTED) bg = RGB(78,70,105);
        fill(d.hDC, d.rcItem, mute || remove ? Card : Canvas);
        round(d.hDC, d.rcItem, bg, 10, bg);
        wchar_t label[80]{}; GetWindowTextW(d.hwndItem, label, 80);
        if(remove) wcscpy_s(label,L"×");
        text(d.hDC, label, d.rcItem, body, disabled ? RGB(76,85,106) : d.CtlID == 1 ? Canvas : muted ? RGB(246,172,186) : Text,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        if (d.itemState & ODS_FOCUS) { RECT f=d.rcItem; InflateRect(&f,-px(3),-px(3)); DrawFocusRect(d.hDC,&f); }
    }
    LRESULT drawSlider(NMCUSTOMDRAW& d) {
        if (d.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
        using namespace ui;
        HWND slider = d.hdr.hwndFrom;
        size_t index = static_cast<size_t>(GetDlgCtrlID(slider)-30);
        RECT r{}, thumb{}; GetClientRect(slider,&r);
        SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        fill(d.hdc, r, Card);
        int center = (thumb.left+thumb.right)/2;
        int top = px(10), bottom = r.bottom-px(10), y=(thumb.top+thumb.bottom)/2;
        COLORREF accent = color(index);
        auto* channel=channelAt(index);
        if(channel && channel->muted) accent=Dim;
        RECT rail{center-px(3),top,center+px(3),bottom};
        round(d.hdc,rail,Border,6,Border);
        RECT filled{rail.left,std::clamp(y,top,bottom),rail.right,bottom};
        if(filled.bottom>filled.top) round(d.hdc,filled,accent,6,accent);
        RECT handle{center-px(14),y-px(6),center+px(14),y+px(6)};
        round(d.hdc,handle,Text,6,Text);
        fill(d.hdc,RECT{center-px(7),y-px(1),center+px(7),y+px(1)},accent);
        if(GetFocus()==slider) { RECT focus=r; InflateRect(&focus,-px(1),-px(1)); DrawFocusRect(d.hdc,&focus); }
        return CDRF_SKIPDEFAULT;
    }
    void paint(HDC dc) {
        using namespace ui;
        RECT r{}; GetClientRect(hwnd_, &r); fill(dc, r, Canvas);
        brand::draw(dc,px(20),px(22),px(34));
        text(dc,L"Volume mixer",rect(66,18,350,32),titleFont_,Text);
        text(dc,L"내 앱의 소리를, 한곳에서.",rect(67,50,340,20),small,Dim);
        round(dc,rect(534,28,86,24),RGB(29,47,45),12,RGB(29,47,45));
        text(dc,master_.output && master_.output->active.load() ? L"●  송출 중" : L"●  준비",rect(534,28,86,24),small,RGB(108,215,195),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        for(size_t i=0;i<strips_.size();++i) {
            int x=20+static_cast<int>(i)*153; size_t index=page_*AppSlots+i;
            auto* channel=channelAt(i); if(!channel) continue;
            auto& c=*channel; COLORREF accent=color(i);
            round(dc,rect(x,132,141,312),Card,16,c.master ? RGB(69,87,116) : Border);
            auto name=c.name; auto ext=name.rfind(L".exe"); if(ext!=std::wstring::npos && ext+4==name.size()) name.resize(ext);
            if(!name.empty()) name[0]=static_cast<wchar_t>(towupper(name[0]));
            round(dc,rect(x+53,148,35,32),RGB(42,48,63),10,RGB(42,48,63));
            if(c.master) {
                text(dc,L"출력",rect(x+100,140,32,20),small,Dim,DT_CENTER|DT_SINGLELINE);
                POINT speaker[]{{px(x+60),px(159)},{px(x+65),px(159)},{px(x+72),px(153)},
                    {px(x+72),px(175)},{px(x+65),px(169)},{px(x+60),px(169)}};
                HBRUSH brush=CreateSolidBrush(accent); HPEN pen=CreatePen(PS_SOLID,px(2),accent);
                auto oldBrush=SelectObject(dc,brush); auto oldPen=SelectObject(dc,pen);
                Polygon(dc,speaker,6);
                Arc(dc,px(x+70),px(156),px(x+82),px(173),px(x+76),px(173),px(x+76),px(156));
                SelectObject(dc,oldPen); SelectObject(dc,oldBrush); DeleteObject(pen); DeleteObject(brush);
            } else if(index<icons_.size() && icons_[index] && icons_[index]->handle)
                DrawIconEx(dc,px(x+55),px(148),icons_[index]->handle,px(32),px(32),0,nullptr,DI_NORMAL);
            round(dc,rect(x+85,173,7,7),!c.muted && c.peak>.001f ? accent : Dim,7,Card);
            text(dc,name.c_str(),rect(x+10,188,121,22),nameFont_,Text,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            text(dc,c.device.c_str(),rect(x+10,212,121,18),small,Dim,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            for(int n=0;n<18;++n) fill(dc,rect(x+106,384-n*6,4,3),!c.muted && c.peak>static_cast<float>(n)/18 ? accent : Border);
        }
        if(channels_.empty()) {
            round(dc,rect(20,132,447,312),Card,16,Border);
            text(dc,L"＋",rect(183,215,120,44),valueFont_,Purple,DT_CENTER|DT_SINGLELINE);
            text(dc,L"믹서에 프로그램을 추가하세요",rect(40,268,407,26),nameFont_,Text,DT_CENTER|DT_SINGLELINE);
            text(dc,L"‘앱 추가’에서 선택한 프로그램만 표시됩니다.",rect(40,302,407,24),small,Dim,DT_CENTER|DT_SINGLELINE);
        }
        auto pages=std::max(size_t(1),(channels_.size()+AppSlots-1)/AppSlots);
        auto count=L"추가한 프로그램 "+std::to_wstring(channels_.size())+L"개";
        text(dc,count.c_str(),rect(20,461,200,24),small,Dim);
        auto pagination=std::to_wstring(page_+1)+L" / "+std::to_wstring(pages);
        text(dc,pagination.c_str(),rect(444,461,66,24),small,Dim,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);
        fill(dc,rect(20,496,600,1),Border);
        text(dc,error_.empty() ? L"믹스 출력은 합쳐서 송출하는 소리와 녹음 파일의 최종 볼륨입니다." : error_.c_str(),
            rect(20,504,600,20),small,error_.empty() ? Dim : RGB(246,172,186),DT_SINGLELINE|DT_END_ELLIPSIS);
        text(dc,L"현재 소리 확인",rect(20,533,400,28),nameFont_,Text);
        text(dc,L"캡처 장치",rect(20,571,90,28),small,Dim,DT_SINGLELINE|DT_VCENTER);
        text(dc,L"듣기 장치",rect(20,609,90,28),small,Dim,DT_SINGLELINE|DT_VCENTER);
        text(dc,L"송출음 확인: CABLE Input 선택 · 마이크 사용 중에는 헤드폰 권장",
            rect(20,678,600,20),small,Dim,DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: fonts(); controls(); refresh(); SetTimer(hwnd_, 1, 100, nullptr); return 0;
        case WM_DRAWITEM: drawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE;
        case WM_NOTIFY: {
            auto* n = reinterpret_cast<NMHDR*>(lp);
            if (n->code == NM_CUSTOMDRAW && n->idFrom >= 30 && n->idFrom < 34)
                return drawSlider(*reinterpret_cast<NMCUSTOMDRAW*>(lp));
            break;
        }
        case WM_TIMER:
            loopback_.sync();
            if (!IsWindowVisible(hwnd_)) return 0;
            if (++tick_ % 20 == 0 && !dragging_) refresh(); else {
                sync(); RECT r = ui::rect(20, 238, 600, 158); InvalidateRect(hwnd_, &r, FALSE);
            }
            return 0;
        case WM_VSCROLL:
            for (size_t i = 0; i < strips_.size(); ++i) if (reinterpret_cast<HWND>(lp) == strips_[i].slider && channelAt(i)) {
                dragging_ = LOWORD(wp) == TB_THUMBTRACK;
                int pos = static_cast<int>(SendMessageW(strips_[i].slider, TBM_GETPOS, 0, 0));
                if (FAILED(channelAt(i)->setLevel((100-pos)/100.0f))) error_ = L"볼륨 변경에 실패했습니다. 새로고침 후 다시 시도하세요.";
                sync(); InvalidateRect(hwnd_, nullptr, FALSE); break;
            }
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 70) { loopback_.toggle(); return 0; }
            if (id == Record || id == Broadcast) { SendMessageW(GetParent(hwnd_), WM_COMMAND, id, 0); return 0; }
            if (id == 1) {
                mixer_dialogs::Program selected;
                if (mixer_dialogs::pickProgram(hwnd_, selected,added_.keys())) {
                    added_.add(selected.key,selected.name);
                    selectedIcons_[selected.key]=selected.icon;
                    refresh();
                    for (size_t i = 0; i < channels_.size(); ++i) if (channels_[i].id == selected.key) { page_ = i/AppSlots; break; }
                }
            } else if (id == 2) { refresh(); loopback_.refresh(); }
            else if (id == 3 && page_) --page_;
            else if (id == 4 && (page_+1)*AppSlots < channels_.size()) ++page_;
            else if(id>=50 && id<53 && page_*AppSlots+id-50<channels_.size()) {
                size_t index=page_*AppSlots+id-50;
                SetFocus(GetDlgItem(hwnd_,1));
                selectedIcons_.erase(channels_[index].id);
                added_.remove(index); refresh();
            }
            else if (id >= 40 && id < 44 && channelAt(id-40)) {
                auto& c = *channelAt(id-40); c.read();
                if (FAILED(c.setMute(!c.muted))) error_ = L"음소거 변경에 실패했습니다. 새로고침 후 다시 시도하세요.";
            }
            sync(); InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp); SetTextColor(dc, Text); SetBkColor(dc, Card);
            return reinterpret_cast<LRESULT>(cardBrush_);
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd_, &ps); RECT r{}; GetClientRect(hwnd_, &r);
            HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
            auto old = SelectObject(buffer, bitmap); paint(buffer);
            BitBlt(dc, 0, 0, r.right, r.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old); DeleteObject(bitmap); DeleteDC(buffer); EndPaint(hwnd_, &ps); return 0;
        }
        case WM_DESTROY: KillTimer(hwnd_, 1); loopback_.stop(); channels_.clear(); return 0;
        }
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<SessionPanel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<SessionPanel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd_ = h; SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return self ? self->message(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
    }
public:
    ~SessionPanel() {
        for(auto f : {titleFont_, valueFont_, nameFont_}) if(f) DeleteObject(f);
        DeleteObject(cardBrush_);
    }
    HWND create(HWND owner, mix::OutputControl& output) {
        master_.master=true; master_.id=L"<mix-output>"; master_.name=L"믹스 출력"; master_.output=&output;
        WNDCLASSW wc{}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"PortableMixerSessionPanel"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        return CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Windows 프로그램 볼륨 믹서", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, ui::px(Width), ui::px(Height), owner, nullptr, wc.hInstance, this);
    }
    void dpiChanged() {
        loopback_.sync();
        // Recreate only controls, retaining COM sessions and current page.
        HWND child = GetWindow(hwnd_, GW_CHILD);
        while (child) { HWND next = GetWindow(child, GW_HWNDNEXT); DestroyWindow(child); child = next; }
        fonts();
        SetWindowPos(hwnd_, nullptr, 0, 0, ui::px(Width), ui::px(Height), SWP_NOZORDER);
        controls(); sync(); InvalidateRect(hwnd_, nullptr, TRUE);
    }
};
