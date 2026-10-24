#pragma once
#include "mixer_core.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace mix {
class WavWriter {
    std::ofstream file_;
    uint32_t bytes_ = 0;
    void word(uint16_t v) { char b[2]{char(v), char(v >> 8)}; file_.write(b, 2); }
    void dword(uint32_t v) { word(uint16_t(v)); word(uint16_t(v >> 16)); }
    void header() {
        file_.seekp(0);
        file_.write("RIFF", 4); dword(36 + bytes_); file_.write("WAVEfmt ", 8);
        dword(16); word(1); word(2); dword(Rate); dword(Rate*4); word(4); word(16);
        file_.write("data", 4); dword(bytes_);
        if (!file_) throw std::runtime_error("WAV header write failed");
    }
public:
    explicit WavWriter(const std::filesystem::path& path)
        : file_(path, std::ios::binary | std::ios::trunc) {
        if (!file_) throw std::runtime_error("Cannot open WAV output");
        header();
    }
    WavWriter(const WavWriter&) = delete;
    void write(const Block& block) {
        constexpr uint32_t size = BlockFrames * 4;
        if (bytes_ > std::numeric_limits<uint32_t>::max() - 36 - size)
            throw std::runtime_error("WAV RIFF 4 GiB limit reached; start a new recording");
        std::array<char, size> data{};
        size_t pos = 0;
        for (auto frame : block) for (float v : {frame.l, frame.r}) {
            const auto sample = static_cast<uint16_t>(toPcm(v));
            data[pos++] = char(sample); data[pos++] = char(sample >> 8);
        }
        file_.write(data.data(), data.size());
        if (!file_) throw std::runtime_error("WAV write failed (disk full or removed)");
        bytes_ += size;
    }
    void finish() {
        if (!file_.is_open()) return;
        header(); file_.flush();
        if (!file_) throw std::runtime_error("WAV flush failed");
        file_.close();
        if (file_.fail()) throw std::runtime_error("WAV close failed");
    }
    ~WavWriter() { try { finish(); } catch (...) {} }
};
} // namespace mix
