#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mix {
constexpr int Rate = 48000;
constexpr int BlockFrames = 480;
struct Frame { float l = 0, r = 0; };
using Block = std::array<Frame, BlockFrames>;

// Single audio-thread ownership. Tagged slots prevent replay after wraparound.
class Timeline {
    struct Slot { int64_t tag = -1; Frame value{}; };
    std::vector<Slot> slots_;
    int64_t consumed_ = 0;
public:
    uint64_t late = 0, overflow = 0, missing = 0;
    explicit Timeline(size_t capacity = 32768) : slots_(capacity) {
        if (!capacity) throw std::invalid_argument("Timeline capacity is zero");
    }
    void put(int64_t first, const int16_t* pcm, size_t frames, bool silent) {
        for (size_t i = 0; i < frames; ++i) {
            const int64_t t = first + static_cast<int64_t>(i);
            if (t < consumed_) { ++late; continue; }
            if (t - consumed_ >= static_cast<int64_t>(slots_.size())) { ++overflow; continue; }
            auto& s = slots_[static_cast<size_t>(t) % slots_.size()];
            s.tag = t;
            s.value = silent ? Frame{} : Frame{pcm[2*i] / 32768.0f, pcm[2*i+1] / 32768.0f};
        }
    }
    Frame take(int64_t t) {
        if (t < consumed_) return {};
        auto& s = slots_[static_cast<size_t>(t) % slots_.size()];
        consumed_ = t + 1;
        if (s.tag != t) { ++missing; return {}; }
        s.tag = -1;
        return s.value;
    }
};

inline int64_t samplePosition(uint64_t stamp100ns, uint64_t epoch100ns) {
    const long double delta = static_cast<long double>(stamp100ns) - epoch100ns;
    return static_cast<int64_t>(std::llround(delta * Rate / 10000000.0L));
}
inline float finiteSample(float v) { return std::isfinite(v) ? v : 0.0f; }
inline int16_t toPcm(float v) {
    v = std::clamp(finiteSample(v), -1.0f, 1.0f);
    return static_cast<int16_t>(std::lround(v * (v < 0 ? 32768.0f : 32767.0f)));
}
struct Meter { float process = 0, mic = 0, output = 0; uint64_t clipped = 0; };
class Mixer {
    float processGain_ = 1, micGain_ = 1;
public:
    Meter render(Timeline& process, Timeline& mic, int64_t first,
                 float processGain, float micGain, Block& out) {
        Meter m;
        processGain = std::clamp(finiteSample(processGain), 0.0f, 2.0f);
        micGain = std::clamp(finiteSample(micGain), 0.0f, 2.0f);
        const float dp = (processGain - processGain_) / BlockFrames;
        const float dm = (micGain - micGain_) / BlockFrames;
        for (int i = 0; i < BlockFrames; ++i) {
            processGain_ += dp; micGain_ += dm;
            const Frame p = process.take(first+i), v = mic.take(first+i);
            m.process = std::max(m.process, std::max(std::abs(p.l), std::abs(p.r)));
            m.mic = std::max(m.mic, std::max(std::abs(v.l), std::abs(v.r)));
            Frame f{p.l*processGain_ + v.l*micGain_, p.r*processGain_ + v.r*micGain_};
            if (std::abs(f.l) > 1 || std::abs(f.r) > 1) ++m.clipped;
            f.l = std::clamp(finiteSample(f.l), -1.0f, 1.0f);
            f.r = std::clamp(finiteSample(f.r), -1.0f, 1.0f);
            m.output = std::max(m.output, std::max(std::abs(f.l), std::abs(f.r)));
            out[i] = f;
        }
        processGain_ = processGain; micGain_ = micGain;
        return m;
    }
};
} // namespace mix
