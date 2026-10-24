#include "mixer_core.hpp"
#include "wav_writer.hpp"
#include "mix_output.hpp"
#include "../shared/pcm_ring.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int assertions = 0;
void require(bool condition, const char* message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a-b) < 0.0001f; }
std::vector<int16_t> constant(int frames, int16_t left, int16_t right) {
    std::vector<int16_t> v(static_cast<size_t>(frames)*2);
    for (int i = 0; i < frames; ++i) { v[2*i] = left; v[2*i+1] = right; }
    return v;
}
uint32_t u32(const std::vector<unsigned char>& v, size_t n) {
    return uint32_t(v[n]) | (uint32_t(v[n+1]) << 8) | (uint32_t(v[n+2]) << 16) | (uint32_t(v[n+3]) << 24);
}
void timelineTests() {
    auto pcm = constant(8, 8192, -16384);
    mix::Timeline timeline(8);
    timeline.put(-2, pcm.data(), 8, false);
    require(timeline.late == 2, "negative pre-epoch samples must be discarded");
    auto a = timeline.take(0);
    require(near(a.l, 0.25f) && near(a.r, -0.5f), "stereo PCM decode");
    require(near(timeline.take(0).l, 0), "consumed data must not be replayed");
    for (int i = 1; i < 8; ++i) timeline.take(i);
    require(timeline.missing == 2, "missing packet tail becomes silence");
    require(near(timeline.take(8).l, 0), "old ring slot cannot replay after wrap");
    timeline.put(0, pcm.data(), 1, false);
    require(timeline.late == 3, "late arrival must not change emitted audio");
    timeline.put(30, pcm.data(), 8, false);
    require(timeline.overflow == 8, "future data beyond bounded ring must be counted");
    timeline.put(9, nullptr, 1, true);
    require(near(timeline.take(9).l, 0), "silent flag does not dereference null");
    timeline.put(10, pcm.data(), 1, false);
    require(near(timeline.take(10).r, -0.5f), "ring still works after gaps and wrap");
    require(mix::samplePosition(100010000, 100000000) == 48, "QPC to 48 kHz conversion");
    require(mix::samplePosition(99990000, 100000000) == -48, "QPC before epoch stays signed");
    bool rejected = false;
    try { mix::Timeline invalid(0); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "zero capacity must be rejected");
}
void mixingTests() {
    auto p = constant(1440, 16384, -16384), v = constant(1440, 8192, 8192);
    mix::Timeline process, mic; mix::Mixer mixer; mix::Block output;
    process.put(0, p.data(), 1440, false); mic.put(0, v.data(), 1440, false);
    const auto first = mixer.render(process, mic, 0, 1, 1, output);
    require(near(output[0].l, 0.75f) && near(output[479].r, -0.25f), "independent source sum");
    require(near(first.process, 0.5f) && near(first.mic, 0.25f), "input meters");
    require(first.clipped == 0 && near(first.output, 0.75f), "output meter");
    mixer.render(process, mic, 480, 0, 2, output);
    require(near(output[479].l, 0.5f) && near(output[479].r, 0.5f), "gain ramp reaches independent targets");
    require(output[0].l > output[479].l, "gain changes ramp across a block");
    mixer.render(process, mic, 960, 0, 2, output);
    require(near(output[0].l, 0.5f) && near(output[479].l, 0.5f), "stable independent mute and gain");
    mix::Timeline p2, m2; mix::Mixer clipMixer;
    p2.put(0, p.data(), 480, false); m2.put(0, p.data(), 480, false);
    const auto clipped = clipMixer.render(p2, m2, 0, 2, 2, output);
    require(clipped.clipped == 480, "clip counter counts frames");
    require(output[479].l == 1 && output[479].r == -1, "hard clipping contains PCM range");
    mix::Timeline empty1, empty2;
    clipMixer.render(empty1, empty2, 0, 1, 1, output);
    for (const auto f : output) require(f.l == 0 && f.r == 0, "missing inputs are silence");
    require(mix::toPcm(-1) == -32768 && mix::toPcm(1) == 32767, "PCM full scale");
    require(mix::toPcm(4) == 32767 && mix::toPcm(-4) == -32768, "PCM clamp");
    require(mix::toPcm(std::numeric_limits<float>::quiet_NaN()) == 0, "NaN containment");
}
void wavTests(const std::filesystem::path& path) {
    mix::Block block{}; block[0] = {1, -1};
    {
        mix::WavWriter writer(path); writer.write(block); writer.write(block); writer.finish(); writer.finish();
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
    require(data.size() == 44 + 480*4*2, "WAV size");
    require(std::string(data.begin(), data.begin()+4) == "RIFF", "RIFF signature");
    require(u32(data, 4) == data.size()-8, "RIFF patched size");
    require(u32(data, 24) == 48000 && u32(data, 28) == 192000, "WAV sample rate and byte rate");
    require(u32(data, 40) == 3840, "WAV patched data size");
    require(data[44] == 0xff && data[45] == 0x7f && data[46] == 0 && data[47] == 0x80, "PCM little-endian stereo");
    bool rejected = false;
    try { mix::WavWriter invalid(path / "absent" / "bad.wav"); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid output path is reported");
}
void outputFaderTests() {
    mix::OutputControl output;
    output.gain=.5f;
    mix::OutputFader fader(output.target());
    mix::Block block; block.fill({.8f,-.4f});
    float peak=fader.apply(block,output);
    require(near(peak,.4f) && near(block.back().l,.4f) && near(block.back().r,-.2f),"Master gain must scale the mixed stereo signal and its meter");
    output.muted=true; block.fill({.8f,-.4f}); fader.apply(block,output);
    require(block.front().l>0 && near(block.back().l,0),"Mute must ramp over one block without a discontinuous gain step");
    block.fill({.8f,-.4f}); peak=fader.apply(block,output);
    require(near(peak,0),"Muted outgoing audio must be silent");
    for(const auto& frame:block) require(near(frame.l,0) && near(frame.r,0),"Mute must silence both output channels");
    output.gain=.25f; block.fill({.8f,-.4f});
    require(near(fader.apply(block,output),0),"Changing the fader while muted must not leak audio");
    output.muted=false; block.fill({.8f,-.4f}); fader.apply(block,output);
    require(block.front().l<block.back().l && near(block.back().l,.2f),"Unmute must restore the selected gain with a ramp");
    output.muted=true; mix::OutputFader restarted(output.target()); block.fill({1,1});
    require(near(restarted.apply(block,output),0),"Starting a mix while muted must not leak an initial audio block");
}
void virtualMicRingTests() {
    pm::PcmRing ring;
    pm::Cursor a, b;
    int16_t input[480], output[480];
    for (int i = 0; i < 480; ++i) input[i] = static_cast<int16_t>(i - 240);
    ring.read(a, output, 480);
    for (auto value : output) require(value == 0, "empty virtual mic is silent");
    ring.write(input, 480);
    ring.read(a, output, 480);
    for (int i = 0; i < 480; ++i) require(output[i] == input[i], "virtual mic preserves PCM samples");
    ring.read(b, output, 480);
    for (int i = 0; i < 480; ++i) require(output[i] == input[i], "independent capture clients receive same audio");
    ring.read(a, output, 480);
    for (auto value : output) require(value == 0, "underrun does not replay old audio");
    ring.reset();
    ring.read(b, output, 480);
    for (auto value : output) require(value == 0, "writer close clears previous conversation");
    for (int block = 0; block < 20; ++block) {
        for (auto& value : input) value = static_cast<int16_t>(block);
        ring.write(input, 480);
    }
    ring.read(a, output, 480);
    for (auto value : output) require(value == 18, "late capture skips to bounded live window");
    ring.read(a, output, 480);
    for (auto value : output) require(value == 19, "ring wrap retains latest audio in order");
    ring.reset();
    for (auto& value : input) value = 123;
    ring.write(input, 480); ring.read(a, output, 480);
    for (auto value : output) require(value == 123, "writer restart reinitializes existing capture cursor");
}
}
int main(int argc, char** argv) {
    try {
        timelineTests(); mixingTests(); outputFaderTests(); virtualMicRingTests();
        const auto path = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("core-test.wav");
        wavTests(path);
        std::cout << "PASS: " << assertions << " assertions; WAV artifact: " << path.string() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
