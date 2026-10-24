#pragma once
#include "mix_output.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mix {
struct Input {
    std::wstring key;
    std::vector<uint32_t> pids;
    std::shared_ptr<OutputControl> control;
};
// UI publishes process membership; audio workers own capture and DSP.
class Inputs {
    mutable std::mutex mutex_;
    std::vector<Input> inputs_;
public:
    void set(std::vector<Input> inputs) { std::lock_guard<std::mutex> lock(mutex_); inputs_ = std::move(inputs); }
    std::vector<Input> snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return inputs_; }
};
}
