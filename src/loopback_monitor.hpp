#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class LoopbackMonitor {
    HANDLE stop_{};
    std::thread worker_;
    mutable std::mutex mutex_;
    std::wstring status_ = L"VB-CABLE의 믹스 또는 다른 출력 장치를 들어볼 수 있습니다.";
    void status(std::wstring text);
    void run(std::wstring source, std::wstring destination) noexcept;
public:
    std::atomic<bool> active{false};
    LoopbackMonitor();
    ~LoopbackMonitor();
    LoopbackMonitor(const LoopbackMonitor&) = delete;
    void start(std::wstring source, std::wstring destination);
    void stop();
    std::wstring statusText() const;
};
