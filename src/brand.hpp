#pragma once
#include <windows.h>
#include "resource.h"

namespace brand {
// Resource icons loaded with LR_SHARED are owned by Windows for the process lifetime.
inline HICON icon(int size) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_PORTABLE_MIXER), IMAGE_ICON, size, size, LR_SHARED));
}
inline void draw(HDC dc, int x, int y, int size) {
    DrawIconEx(dc, x, y, icon(256), size, size, 0, nullptr, DI_NORMAL);
}
}
