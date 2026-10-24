#pragma once
#include <windows.h>
#include <shellapi.h>
#include <map>
#include <memory>
#include <string>

namespace app_icons {
struct Icon {
    HICON handle = nullptr;
    explicit Icon(HICON value) : handle(value) {}
    ~Icon() { if (handle) DestroyIcon(handle); }
    Icon(const Icon&) = delete;
};
using Image = std::shared_ptr<Icon>;
// Cache by executable path, not PID: Windows can reuse process IDs.
class Cache {
    std::map<std::wstring, Image> images_;
public:
    Image get(DWORD pid) {
        std::wstring path;
        if (pid) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                wchar_t buffer[32768]{}; DWORD length = 32768;
                if (QueryFullProcessImageNameW(process, 0, buffer, &length)) path.assign(buffer, length);
                CloseHandle(process);
            }
        }
        const std::wstring key = !pid ? L"<system>" : path.empty() ? L"<unavailable>" : path;
        auto existing = images_.find(key);
        if (existing != images_.end()) return existing->second;
        HICON icon = nullptr;
        if (!path.empty()) {
            SHFILEINFOW info{};
            if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON)) icon = info.hIcon;
        }
        if (!pid) {
            SHSTOCKICONINFO info{}; info.cbSize = sizeof(info);
            if (SUCCEEDED(SHGetStockIconInfo(SIID_AUDIOFILES, SHGSI_ICON | SHGSI_LARGEICON, &info))) icon = info.hIcon;
        }
        if (!icon) icon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        auto result = std::make_shared<Icon>(icon);
        // Live controls keep shared ownership when old cache entries are evicted.
        if (images_.size() >= 64) images_.clear();
        images_.emplace(key, result); return result;
    }
};
} // namespace app_icons
