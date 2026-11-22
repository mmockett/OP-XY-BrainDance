// Brain Dance's sample analysis checks (harvest.h), compiled for the host and
// run by python sdk.py test --machine <folder>. The samples are made here, so
// the repository holds no audio: a kick, a snare, a tick, a pitched blip, a
// held 220 Hz tone and held noise, in mono and in stereo; and silence.
// - the SDK's callbacks: a sample declined when empty, storage sized to it,
//   no step reading more frames than it's given;
// - each kind found where it was made, the tone's period within a frame;
// - silence finds nothing;
// - playing with the result: within the declared peak, lanes take snippets,
//   it differs from the synthesis, and a trim that holds no snippet plays the
//   synthesis exactly; and a key and settings always play the same.
#include "../sample.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using opxy::i16;
using opxy::i32;
using opxy::u32;
int failures = 0;
void fail(const char* what) { if (failures++ < 20) std::printf("FAIL %s\n", what); }

const double rate = 44100, pi = 3.14159265358979;
const u32 kick_at = 11025, snare_at = 44100, tick_at = 70560, blip_at = 88200, tone_at = 114660, tone_end = 158760,
          noise_at = 167580, noise_end = 211680, total = 220500;

std::vector<double> make() {
    std::vector<double> x(total, 0.0);
    u32 noise = 12345;
    auto white = [&]() { noise = noise * 1664525u + 1013904223u; return (int(noise >> 8) - 8388608) / 8388608.0; };
    double ph = 0;
    for (u32 i = 0; i < 13230; ++i) {   // kick: 150 Hz falling to 55, 300 ms
        const double t = i / rate, f = 55 + 95 * std::exp(-t / 0.012);
        ph += 2 * pi * f / rate;
        x[kick_at + i] = 0.9 * std::sin(ph) * std::exp(-t * 23);
    }
    for (u32 i = 0; i < 8820; ++i) {    // snare: noise and a little body, 200 ms
        const double t = i / rate;
        x[snare_at + i] = (0.6 * white() + 0.15 * std::sin(2 * pi * 190 * t)) * std::exp(-t * 46);
    }
    for (u32 i = 0; i < 132; ++i)       // tick: 3 ms of 6 kHz
        x[tick_at + i] = 0.7 * std::sin(2 * pi * 6000 * i / rate) * (1 - i / 132.0);
    for (u32 i = 0; i < 6615; ++i) {    // blip: 1 kHz, 150 ms
        const double t = i / rate;
        x[blip_at + i] = 0.7 * std::sin(2 * pi * 1000 * t) * std::exp(-t * 69) * std::min(1.0, i / 40.0);
    }
    for (u32 i = tone_at; i < tone_end; ++i) {   // a held tone: 220 Hz
        const double t = (i - tone_at) / rate, fade = std::min(1.0, std::min(t / 0.02, (tone_end - i) / 882.0));
        x[i] = 0.5 * std::sin(2 * pi * 220 * t) * fade;
    }
    for (u32 i = noise_at; i < noise_end; ++i) {  // held noise
        const double fade = std::min(1.0, std::min((i - noise_at) / 441.0, (noise_end - i) / 441.0));
        x[i] = 0.3 * white() * fade;
    }
    return x;
}

std::vector<i16> pcm(const std::vector<double>& x, u32 channels) {
    std::vector<i16> out(x.size() * channels);
    for (size_t i = 0; i < x.size(); ++i)
        for (u32 c = 0; c < channels; ++c) out[i * channels + c] = i16(std::lround(x[i] * (c ? 0.8 : 1.0) * 32767));
    return out;
}

braindance::Preparation job;
// Runs the analysis as the SDK does, 4096 frames a step.
bool analyse(const opxy::SampleView& view, std::vector<u32>& storage, opxy::SampleAnalysis& result) {
    const u32 words = braindance::analysis_words(view);
    if (!words) return false;
    storage.assign(words, 0xdeadbeefu);
    std::memset(&job, 0, sizeof job);
    if (!braindance::prepare_begin(job, view, storage.data(), words)) { fail("prepare_begin refused a sample"); return false; }
    u32 cursor = 0, steps = 0;
    opxy::PrepareStatus status;
    do {
        const u32 before = cursor;
        status = braindance::prepare_step(job, 4096, cursor);
        if (job.stage == 0 && cursor - before > 4096) fail("a step read more frames than it was given");
        if (++steps > 100000) { fail("the analysis never finished"); return false; }
    } while (status == opxy::prepare_running);
    if (status != opxy::prepare_done) { fail("the analysis failed"); return false; }
    if (!braindance::prepared(job, result)) { fail("prepared refused a finished analysis"); return false; }
    if (result.words != storage.data() || result.word_count > words) fail("the result is outside its storage");
    return true;
}

// The first snippet of a kind, or null.
const u32* first(const opxy::SampleAnalysis& a, u32 kind) {
    for (u32 i = 0; i < braindance::harvest::count_of(a.words); ++i) {
        const u32* r = a.words + braindance::harvest::header_words + 4 * i;
        if (braindance::harvest::kind_of(r) == kind) return r;
    }
    return 0;
}
bool found_near(const opxy::SampleAnalysis& a, u32 kind, u32 from, u32 to) {
    for (u32 i = 0; i < braindance::harvest::count_of(a.words); ++i) {
        const u32* r = a.words + braindance::harvest::header_words + 4 * i;
        if (braindance::harvest::kind_of(r) == kind && braindance::harvest::start_of(r) >= from && braindance::harvest::start_of(r) < to) return true;
    }
    return false;
}

void check_finds(u32 channels) {
    const std::vector<i16> data = pcm(make(), channels);
    const opxy::SampleView view = {data.data(), total, 44100, channels};
    std::vector<u32> storage;
    opxy::SampleAnalysis a;
    if (!analyse(view, storage, a)) return;
    using namespace braindance::harvest;
    std::printf("%u channel(s): %u snippets\n", channels, count_of(a.words));
    for (u32 i = 0; i < count_of(a.words); ++i) {
        const u32* r = a.words + header_words + 4 * i;
        std::printf("  kind %u at %6u, %5u frames, gain %5u, score %4u, period %.2f, %u cycles\n", kind_of(r), start_of(r),
                    length_of(r), gain_of(r), score_of(r), period_of(r) / 256.0, 1u << cycles_of(r));
        if (start_of(r) + length_of(r) > total) fail("a snippet runs past the sample");
    }
    if (!found_near(a, kLowHit, kick_at - 512, kick_at + 512)) fail("no low hit at the kick");
    if (!found_near(a, kSnapHit, snare_at - 512, snare_at + 512)) fail("no snap hit at the snare");
    if (!found_near(a, kClickHit, tick_at - 512, tick_at + 512)) fail("no click hit at the tick");
    if (!found_near(a, kBlipHit, blip_at - 512, blip_at + 512)) fail("no blip hit at the blip");
    if (!found_near(a, kToneLoop, tone_at, tone_end)) fail("no tone loop in the held tone");
    if (!found_near(a, kNoiseLoop, noise_at, noise_end)) fail("no noise loop in the held noise");
    if (found_near(a, kToneLoop, kick_at, snare_at)) fail("a tone loop from the kick's falling tail");
    bool tuned = false;
    for (u32 i = 0; i < count_of(a.words); ++i) {
        const u32* r = a.words + header_words + 4 * i;
        if (kind_of(r) == kToneLoop && start_of(r) >= tone_at && start_of(r) < tone_end && std::fabs(period_of(r) / 256.0 - 44100 / 220.0) < 1.0) tuned = true;
    }
    if (!tuned) fail("the tone loop's period is not 220 Hz's");
    const u32* blip = first(a, kBlipHit);
    if (blip && std::fabs(period_of(blip) / 256.0 - 44.1) > 1.0) fail("the blip's period is not 1 kHz's");
}

// Plays one key for blocks with or without the analysis, and a trim; returns the left channel.
std::vector<i32> play(const opxy::SampleAnalysis* a, u32 begin, u32 end, u32 blocks, double chaos, u32* took, double algo = 0.5 / 60) {   // lanes, variant 0
    std::memset(&braindance::engine::clock, 0, sizeof braindance::engine::clock);
    std::memset(&braindance::engine::space, 0, sizeof braindance::engine::space);
    braindance::Voice voice;
    std::memset(&voice, 0, sizeof voice);
    braindance::initialize(voice, a ? a->sample : opxy::SampleView{0, 0, 44100, 1});
    braindance::start(voice);
    const double c[8] = {algo, 0.8, chaos, 0.3, 0.5, 0.3, 0, 0};
    i32 params[8];
    for (int i = 0; i < 8; ++i) params[i] = i32(c[i] * 2147483647.0);
    i32 out[128];
    u32 pitch[64];
    for (int i = 0; i < 64; ++i) pitch[i] = u32(261.63 / 44100 * 4294967296.0);
    std::vector<i32> left;
    for (u32 b = 0; b < blocks; ++b) {
        opxy::SampleBlock blk = opxy::SampleBlock();
        blk.output = out; blk.parameters = params; blk.pitch = pitch;
        blk.transport.bpm_x10 = 1200;
        blk.output_channels = 2;
        blk.analysis = a;
        blk.region.begin = begin;
        blk.region.end = end;
        blk.region.level = opxy::sample_unity_level;
        braindance::render_sample(voice, blk);
        for (int i = 0; i < 128; ++i)
            if (out[i] > 1073741824 || out[i] < -1073741824) { fail("above the declared peak"); return left; }
        for (int i = 0; i < 64; ++i) left.push_back(out[2 * i]);
        if (took) for (u32 k = 0; k < braindance::lane_count; ++k) if (voice.lane[k].src) took[k] |= 1u << voice.lane[k].src;
    }
    return left;
}

double energy(const std::vector<i32>& x) {
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i) e += double(x[i]) * x[i];
    return e / (x.size() + 1);
}
}

int main() {
    // declined: nothing to analyse
    if (braindance::analysis_words(opxy::SampleView{0, 0, 44100, 1})) fail("analysis_words took an empty sample");
    check_finds(1);
    check_finds(2);
    // silence: nothing found
    {
        std::vector<i16> quiet(88200, 0);
        std::vector<u32> storage;
        opxy::SampleAnalysis a;
        if (analyse(opxy::SampleView{quiet.data(), 88200, 44100, 1}, storage, a) && braindance::harvest::count_of(a.words))
            fail("silence gave snippets");
    }
    // playing with the snippets
    const std::vector<i16> data = pcm(make(), 1);
    const opxy::SampleView view = {data.data(), total, 44100, 1};
    std::vector<u32> storage;
    opxy::SampleAnalysis a;
    if (analyse(view, storage, a)) {
        const u32 blocks = u32(44100 * 8 / 64);
        u32 took[braindance::lane_count] = {0, 0, 0, 0};
        const std::vector<i32> sampled = play(&a, 0, total, blocks, 0.3, took);
        const std::vector<i32> synth = play(0, 0, 0, blocks, 0.3, 0);
        const std::vector<i32> outside = play(&a, total - 64, total, blocks, 0.3, 0);   // a trim with no snippet in it
        if (sampled == synth) fail("the sample changed nothing");
        if (outside != synth) fail("a trim holding no snippet didn't play the synthesis");
        if (energy(sampled) < energy(synth) / 50) fail("the sample's lanes are nearly silent");
        if (play(&a, 0, total, blocks, 0.3, 0) != sampled) fail("the same key and settings played differently");
        const char* names[4] = {"low", "snap", "click", "blip"};
        for (u32 k = 0; k < braindance::lane_count; ++k) {
            std::printf("lane %s took:%s%s%s\n", names[k], (took[k] & 2) ? " a hit" : "", (took[k] & 4) ? " a tone loop" : "", (took[k] & 8) ? " a noise loop" : "");
            if (!(took[k] & 2)) fail("a lane never took its hit");
        }
        // trimmed to the held tone and noise: the low and blip lanes loop the tone, snap and click the noise
        // (no tone loop may come from the kick: its tail is too low and falling to loop)
        u32 looped[braindance::lane_count] = {0, 0, 0, 0};
        const std::vector<i32> loops = play(&a, tone_at, noise_end, blocks, 0.3, looped, 30.5 / 60);   // euclid: every lane plays
        if (energy(loops) < energy(synth) / 50) fail("the loops are nearly silent");
        if (!(looped[braindance::kLow] & 4) || !(looped[braindance::kBlip] & 4)) fail("low and blip didn't loop the tone");
        if (!(looped[braindance::kSnap] & 8) || !(looped[braindance::kClick] & 8)) fail("snap and click didn't loop the noise");
        // the loudest chaos, every snippet in play: still within the peak
        play(&a, 0, total, blocks, 1.0, 0);
        // shred with the sample: its sweeps, blocks and clicks come from the snippets, the same every time
        u32 shredded[braindance::lane_count] = {0, 0, 0, 0};
        const std::vector<i32> shred = play(&a, 0, total, blocks, 0.5, shredded, 50.5 / 60);
        if (!(shredded[braindance::kLow] & 2) || !(shredded[braindance::kSnap] & 2)) fail("shred didn't play the sample's hits");
        if (play(&a, 0, total, blocks, 0.5, 0, 50.5 / 60) != shred) fail("shred with the sample played differently twice");
    }
    if (!failures) std::puts("PASS brain dance harvest");
    return failures ? 1 : 0;
}
