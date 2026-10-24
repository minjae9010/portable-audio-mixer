#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include "mix_output.hpp"
#include "mix_monitor.hpp"
#include "mix_inputs.hpp"

class AudioEngine {
    HANDLE stop_ = nullptr;
    std::thread thread_;
    mutable std::mutex textMutex_;
    std::wstring text_ = L"Idle.";
    void status(std::wstring text);
    void run(std::wstring micId, std::wstring path, std::wstring outputId) noexcept;
public:
    mix::OutputControl output;
    MixMonitor monitor;
    mix::Inputs inputs;
    std::atomic<bool> active{false};
    std::atomic<float> processGain{1}, micGain{1};
    std::atomic<float> processPeak{0}, micPeak{0}, outputPeak{0};
    std::atomic<uint64_t> clips{0}, late{0}, missing{0}, overflows{0}, discontinuities{0}, timestampErrors{0};
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    void start(std::wstring micId, std::wstring path, std::wstring outputId);
    void stop();
    std::wstring statusText() const;
};
