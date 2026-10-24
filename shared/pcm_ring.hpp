#pragma once
#ifdef _KERNEL_MODE
using uint64_t = unsigned __int64;
using int16_t = short;
#else
#include <stdint.h>
#include <stddef.h>
#endif

namespace pm {
// Caller supplies synchronization. Fixed storage, no allocation, suitable for
// a kernel DPC. Every capture stream has its own cursor (no destructive reads).
struct Cursor { uint64_t next = 0, generation = 0; };
class PcmRing {
    static constexpr size_t Capacity = 4800; // 100 ms, mono PCM16
    int16_t samples_[Capacity]{};
    uint64_t end_ = 0, generation_ = 1;
public:
    void reset() { end_ = 0; ++generation_; }
    void write(const int16_t* data, size_t count) {
        for (size_t i = 0; i < count; ++i) samples_[end_++ % Capacity] = data[i];
    }
    void read(Cursor& cursor, int16_t* out, size_t count) {
        if (cursor.generation != generation_) {
            cursor.generation = generation_;
            // A newly opened stream starts near live audio, with at most 20 ms queued.
            cursor.next = end_ > 960 ? end_ - 960 : 0;
        }
        if (cursor.next > end_ || end_ - cursor.next > Capacity)
            cursor.next = end_ > 960 ? end_ - 960 : 0;
        for (size_t i = 0; i < count; ++i)
            out[i] = cursor.next < end_ ? samples_[cursor.next++ % Capacity] : 0;
    }
};
}
