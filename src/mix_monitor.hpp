#pragma once
#include "mixer_core.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

// Copies the final mix to the default speakers; never captures another device.
class MixMonitor {
    HANDLE wake_{};
    std::thread worker_;
    std::atomic<bool> stopping_{false}, running_{false}, failed_{false};
    std::atomic<unsigned> revision_{0};
    mutable std::mutex mutex_;
    std::wstring text_;
    std::array<mix::Block, 4> queue_{};
    size_t head_ = 0, count_ = 0;
    void status(std::wstring text);
    void run(std::wstring excludedEndpoint) noexcept;
public:
    std::atomic<bool> enabled{false}, active{false};
    std::atomic<uint64_t> dropped{0};
    MixMonitor();
    ~MixMonitor();
    MixMonitor(const MixMonitor&) = delete;
    void enable(bool value);
    void start(std::wstring excludedEndpoint);
    void push(const mix::Block& block);
    void stop();
    std::wstring statusText() const;
};
