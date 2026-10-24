#pragma once
#include <windows.h>
#include <algorithm>
#include <vector>

namespace ui {
constexpr COLORREF Background = RGB(19, 22, 30), Surface = RGB(29, 33, 44);
constexpr COLORREF Ink = RGB(236, 239, 247), Muted = RGB(139, 149, 171);
constexpr COLORREF Accent = RGB(176, 157, 255), Soft = RGB(37, 43, 57);
constexpr COLORREF Line = RGB(43, 49, 64), Green = RGB(108, 215, 195);
inline UINT dpi = 96;
inline HFONT body{}, heading{}, title{}, small{}, number{};
inline HBRUSH surface = CreateSolidBrush(Surface);
struct Placement { HWND hwnd; int x, y, w, h; };
inline std::vector<Placement> controls;
inline int px(int value) { return MulDiv(value, static_cast<int>(dpi), 96); }
inline HFONT makeFont(int size, int weight = FW_NORMAL, const wchar_t* face = L"맑은 고딕") {
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}
inline void fonts() {
    for (HFONT f : {body, heading, title, small, number}) if (f) DeleteObject(f);
    body = makeFont(13); heading = makeFont(17, FW_BOLD); title = makeFont(28, FW_BOLD, L"Segoe UI");
    small = makeFont(12); number = makeFont(30, FW_SEMIBOLD, L"Segoe UI");
}
inline RECT rect(int x, int y, int w, int h) { return {px(x), px(y), px(x+w), px(y+h)}; }
inline void fill(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color); FillRect(dc, &r, b); DeleteObject(b);
}
inline void round(HDC dc, RECT r, COLORREF color, int radius = 12, COLORREF border = Surface) {
    HBRUSH b = CreateSolidBrush(color); HPEN p = CreatePen(PS_SOLID, 1, border);
    auto oldB = SelectObject(dc, b); auto oldP = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, px(radius), px(radius));
    SelectObject(dc, oldB); SelectObject(dc, oldP); DeleteObject(b); DeleteObject(p);
}
inline void text(HDC dc, const wchar_t* text, RECT r, HFONT font, COLORREF color = Ink, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &r, flags | DT_NOPREFIX); SelectObject(dc, old);
}
inline void meter(HDC dc, int x, int y, int w, float value, COLORREF color) {
    value = std::clamp(value, 0.0f, 1.0f);
    constexpr int segments = 36;
    for (int i = 0; i < segments; ++i) {
        const int left = x + i*w/segments;
        fill(dc, rect(left, y, std::max(1, w/segments-3), 8),
            value > static_cast<float>(i)/segments ? (i > 32 ? RGB(230, 94, 110) : color) : Line);
    }
}
inline void layout() {
    for (auto c : controls) {
        MoveWindow(c.hwnd, px(c.x), px(c.y), px(c.w), px(c.h), TRUE);
        SendMessageW(c.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    }
}
inline void release() {
    for (HFONT f : {body, heading, title, small, number}) if (f) DeleteObject(f);
    DeleteObject(surface);
}
}
