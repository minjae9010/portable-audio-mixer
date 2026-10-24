#pragma once
#include "mixer_core.hpp"
#include <atomic>

namespace mix {
struct OutputControl {
    std::atomic<float> gain{1}, peak{0};
    std::atomic<bool> muted{false}, active{false};
    float target() const { return muted.load() ? 0.0f : std::clamp(finiteSample(gain.load()),0.0f,1.0f); }
};
// Audio-thread owned. The same processed block goes to both output and recording.
class OutputFader {
    float gain_;
public:
    explicit OutputFader(float initial=1) : gain_(std::clamp(finiteSample(initial),0.0f,1.0f)) {}
    float apply(Block& block,const OutputControl& control) {
        const float target=control.target(), start=gain_;
        float peak=0;
        for(size_t i=0;i<block.size();++i) {
            const float gain=start+(target-start)*static_cast<float>(i+1)/static_cast<float>(block.size());
            auto& frame=block[i]; frame.l=finiteSample(frame.l)*gain; frame.r=finiteSample(frame.r)*gain;
            peak=std::max(peak,std::max(std::abs(frame.l),std::abs(frame.r)));
        }
        gain_=target; return peak;
    }
};
} // namespace mix
