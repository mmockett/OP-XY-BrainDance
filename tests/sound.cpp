// Brain Dance's sound checks, compiled for the host and run by
// python sdk.py test --machine <folder>:
// - every block within the declared peak (half scale), through every
//   algorithm, the controls' corners and up to eight keys;
// - every algorithm sounds with one key at its defaults;
// - hits land on the 32nd grid exactly: on the sequencer's song position
//   while it plays (at two tempos), and from the key while it is stopped;
// - more keys play more;
// - the key transposes the sound, and a key and settings always play the same;
// - the sound is pinned: each algorithm's exact output, and shred's on a
//   sample, hash to the values below, so work on its speed can't change it;
// - the short ways round a division give what the division gives.
#include "../sample.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>

namespace {
using opxy::i32;
using opxy::u32;
int failures = 0;
void fail(const char* what) { if (failures++ < 20) std::printf("FAIL %s\n", what); }

struct Settings { double c[8]; };
Settings defaults(u32 algo) {
    Settings s = {{(algo * 10 + 0.5) / 60, 0.5, 0.3, 0.25, 0.4, 0.3, 0, 0}};   // the algorithm's variant 0
    return s;
}

// Plays keys (MIDI notes, all from the start) for blocks, summed; returns the
// left channel, and checks every block's peak.
std::vector<i32> play(const Settings& set, const std::vector<int>& keys, u32 blocks, double bpm, bool playing) {
    std::memset(&braindance::engine::clock, 0, sizeof braindance::engine::clock);
    std::memset(&braindance::engine::space, 0, sizeof braindance::engine::space);
    std::vector<braindance::Voice> voices(keys.size());
    i32 params[8];
    for (int i = 0; i < 8; ++i) params[i] = i32(set.c[i] * 2147483647.0);
    std::vector<i32> left(blocks * 64, 0);
    i32 out[128];
    u32 pitch[64];
    for (size_t k = 0; k < keys.size(); ++k) {
        std::memset(&voices[k], 0, sizeof voices[k]);
        braindance::initialize(voices[k], opxy::SampleView{0, 0, 44100, 1});
        braindance::start(voices[k]);
    }
    for (u32 b = 0; b < blocks; ++b) {
        for (size_t k = 0; k < keys.size(); ++k) {
            const double f = 440 * std::pow(2.0, (keys[k] - 69) / 12.0);
            for (int i = 0; i < 64; ++i) pitch[i] = u32(f / 44100 * 4294967296.0);
            opxy::SampleBlock blk = opxy::SampleBlock();
            blk.output = out; blk.parameters = params; blk.pitch = pitch;
            blk.transport.bpm_x10 = u32(bpm * 10 + 0.5);
            blk.transport.playing = playing;
            blk.transport.ticks96 = playing ? u32(b * 64.0 / 44100 * bpm / 60 * 96) : 0;
            blk.output_channels = 2;
            braindance::render_sample(voices[k], blk);
            for (int i = 0; i < 128; ++i)
                if (out[i] > 1073741824 || out[i] < -1073741824) { fail("above the declared peak"); return left; }
            for (int i = 0; i < 64; ++i) left[b * 64 + i] += out[2 * i] >> 3;
        }
    }
    return left;
}

// The sound, pinned: a hash of the exact output of each algorithm with three
// keys, everything in play (the space, the comb, the clip, dropouts), of
// shred on a sample's tone, and of shred with one key alone, with and
// without the sample (a lone voice rehearses its ticks and works the space
// out early). Integers only, so every host gives the same.
// A change that is meant to leave the sound alone must leave these alone.
u32 early_blocks;   // blocks of the last pinned render that left the space's next block worked out early
u32 pinned(u32 step, bool sampled, bool playing, u32 keys = 3) {
    using namespace opxy;
    using opxy::i16;
    std::memset(&braindance::engine::clock, 0, sizeof braindance::engine::clock);
    std::memset(&braindance::engine::space, 0, sizeof braindance::engine::space);
    static i16 pcm[88200];
    static u32 storage[4096];
    static braindance::Preparation preparation;
    SampleAnalysis analysis = SampleAnalysis();
    if (sampled) {
        for (u32 frame = 0; frame < 88200; ++frame) {   // the SDK's benchmark sample: a triangle with an impulse four times a second
            const u32 phase = (frame * 97u) & 65535u;
            const i32 triangle = phase < 32768u ? static_cast<i32>(phase) : 65535 - static_cast<i32>(phase);
            pcm[frame] = static_cast<i16>(frame % 11025u == 0 ? 24576 : triangle - 16384);
        }
        const SampleView view{pcm, 88200, 44100, 1};
        const u32 words = braindance::analysis_words(view);
        preparation = braindance::Preparation();
        if (!words || words > 4096 || !braindance::prepare_begin(preparation, view, storage, words)) return 0;
        PrepareStatus status = prepare_running;
        u32 cursor = 0;
        for (u32 pass = 0; pass < 100000 && status == prepare_running; ++pass) status = braindance::prepare_step(preparation, 1024, cursor);
        if (status != prepare_done || !braindance::prepared(preparation, analysis)) return 0;
    }
    static braindance::Voice voices[3];
    static const u32 increments[3] = {12740059u, 19088743u, 25480118u};   // C3, G3, C4
    const i32 q = 21474836;   // a hundredth
    const i32 params[8] = {i32((step * 2 + 1) * 17895697u), 70 * q, 40 * q, 45 * q, 40 * q, 70 * q, 70 * q, sampled ? 80 * q : 55 * q};
    u32 pitch[64], hash = 2166136261u;
    i32 out[128];
    for (u32 k = 0; k < 3; ++k) {
        std::memset(&voices[k], 0, sizeof voices[k]);
        braindance::initialize(voices[k], SampleView{pcm, 88200, 44100, 1});
    }
    early_blocks = 0;
    for (u32 b = 0; b < 600; ++b)
        for (u32 k = 0; k < keys; ++k) {
            if (b < k * 40) continue;                    // the keys come in one by one
            if (b == k * 40) braindance::start(voices[k]);
            for (u32 i = 0; i < 64; ++i) pitch[i] = increments[k];
            SampleBlock blk = SampleBlock();
            blk.output = out; blk.parameters = params; blk.pitch = pitch;
            blk.transport.bpm_x10 = 1500;
            blk.transport.playing = playing;
            blk.transport.ticks96 = playing ? b * 64u * 240u / 44100u : 0;   // 150 BPM: 240 96ths a second
            blk.region.begin = 0; blk.region.end = 88200; blk.region.level = sample_unity_level;
            blk.analysis = sampled ? &analysis : 0;
            blk.output_channels = 2;
            braindance::render_sample(voices[k], blk);
            if (braindance::engine::space.early_ready) ++early_blocks;
            for (u32 i = 0; i < 128; ++i) hash = (hash ^ u32(out[i])) * 16777619u;
        }
    return hash;
}

double energy(const std::vector<i32>& x) {
    double e = 0;
    for (size_t i = 0; i < x.size(); ++i) e += double(x[i]) * x[i];
    return e / (x.size() + 1);
}

// Starts of sound after exact silence: where every hard-gated data hit begins.
std::vector<long> starts(const std::vector<i32>& x) {
    std::vector<long> s;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i] != 0 && x[i - 1] == 0) s.push_back(long(i));
    return s;
}

void grid(double bpm, bool playing) {
    // data, one key, no chaos, no glitch, the shortest gates: silence between hits
    Settings set = {{20.5 / 60, 0.6, 0, 0, 0, 0.05, 0, 0}};
    const std::vector<i32> x = play(set, std::vector<int>(1, 48), u32(44100 * 6 / 64), bpm, playing);
    const double s32 = 44100 * 60.0 / bpm / 8;
    const std::vector<long> s = starts(x);
    int on = 0, off = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] < 128) continue;   // a tick the voice's two placing blocks missed, played at once
        double offset = std::fmod(double(s[i]) - (playing ? 0.0 : 64.0), s32);
        if (offset > s32 / 2) offset -= s32;
        if (std::fabs(offset) <= 1.5) ++on;
        else { ++off; if (getenv("BD_DEBUG")) std::printf("  off: sample %ld offset %.1f (s32 %.1f)\n", s[i], offset, s32); }
    }
    char what[96];
    std::snprintf(what, sizeof what, "%s at %.0f BPM: %d hits on the 32nd grid, %d off it", playing ? "playing" : "stopped", bpm, on, off);
    std::printf("%s\n", what);
    if (on < 20 || off > on / 50) fail(what);
}
}

int main() {
    // the peak, through the corners and up to eight keys
    const int chord[8] = {48, 55, 60, 63, 67, 70, 72, 79};
    for (u32 algo = 0; algo < 6; ++algo)
        for (u32 corner = 0; corner < 16; ++corner) {
            Settings set = defaults(algo);
            for (int i = 0; i < 4; ++i) set.c[1 + i] = (corner >> i) & 1 ? 1.0 : 0.0;
            set.c[5] = corner & 1 ? 0.9 : 0.1;
            set.c[6] = corner & 2 ? 1.0 : 0.0;
            set.c[7] = corner & 4 ? 1.0 : 0.0;
            play(set, std::vector<int>(chord, chord + 1 + (corner & 7)), 400, 90 + corner * 10, corner & 1);
        }
    // every one of the sixty steps (an algorithm and its variant): within the peak with four keys and
    // everything up, sounding with one key, and no two variants of an algorithm alike
    for (u32 algo = 0; algo < 6; ++algo) {
        std::vector<std::vector<i32> > heard;
        for (u32 variant = 0; variant < 10; ++variant) {
            Settings set = {{(algo * 10 + variant + 0.5) / 60, 0.9, 0.7, 0.8, 0.5, 0.6, 0.9, 0.6}};
            play(set, std::vector<int>(chord, chord + 4), 500, 170, true);
            Settings plain = defaults(algo);
            plain.c[0] = set.c[0];
            heard.push_back(play(plain, std::vector<int>(1, 48), 1400, 120, true));
            char what[64];
            if (energy(heard.back()) < 1e6) { std::snprintf(what, sizeof what, "algorithm %u variant %u silent with one key", algo, variant); fail(what); }
            for (u32 other = 0; other < variant; ++other)
                if (heard[other] == heard[variant]) { std::snprintf(what, sizeof what, "algorithm %u: variants %u and %u play the same", algo, other, variant); fail(what); }
        }
    }
    // every algorithm sounds
    for (u32 algo = 0; algo < 6; ++algo) {
        if (energy(play(defaults(algo), std::vector<int>(1, 48), 1400, 120, true)) < 1e6) {
            char what[64];
            std::snprintf(what, sizeof what, "algorithm %u silent with one key", algo);
            fail(what);
        }
    }
    // on the grid
    grid(120, true);
    grid(97, true);
    grid(120, false);
    grid(150, false);
    // more keys, more
    for (u32 algo = 0; algo < 6; ++algo) {
        braindance::engine::strikes = 0;
        play(defaults(algo), std::vector<int>(1, 48), 1400, 120, true);
        const u32 one = braindance::engine::strikes;
        braindance::engine::strikes = 0;
        play(defaults(algo), std::vector<int>(chord, chord + 4), 1400, 120, true);
        const u32 four = braindance::engine::strikes;
        std::printf("algorithm %u: %u hits with one key, %u with four\n", algo, one, four);
        if (four < one * 3) {
            char what[64];
            std::snprintf(what, sizeof what, "algorithm %u: four keys no busier than one", algo);
            fail(what);
        }
    }
    // the key transposes it; the same key plays the same
    const std::vector<i32> c = play(defaults(2), std::vector<int>(1, 48), 700, 120, true);
    const std::vector<i32> again = play(defaults(2), std::vector<int>(1, 48), 700, 120, true);
    const std::vector<i32> fsharp = play(defaults(2), std::vector<int>(1, 54), 700, 120, true);
    if (c != again) fail("the same key and settings played differently");
    if (c == fsharp) fail("a different key played the same");
    // the same with everything in play: shred, the space, the comb, dropouts and the clip
    Settings wild = {{50.5 / 60, 0.8, 0.6, 0.7, 0.5, 0.4, 0.8, 0.5}};
    const std::vector<int> two(chord, chord + 2);
    if (play(wild, two, 1400, 180, true) != play(wild, two, 1400, 180, true)) fail("shred with the space played differently twice");
    // the space: silent until its gate opens, then it rings, staggered, and is cut dead when the gate shuts
    {
        using namespace braindance::engine;
        std::memset(&space, 0, sizeof space);
        i32 wet[128];
        double shut = 0, open = 0, after = 0;
        u32 silenced = 0, slices = 0;
        for (u32 b = 0; b < 200; ++b) {
            if (b < 20) for (u32 i = 0; i < 64; ++i) space.bus[i] = (i & 8) ? one / 2 : -one / 2;   // fed, the gate shut
            if (b == 40) { space.gate = 64 * 60; space.chop = 0xaaaaaaaau; space.slice = 128; space.left = 0; }
            if (!space_run(wet)) for (u32 i = 0; i < 128; ++i) wet[i] = 0;   // shut throughout: it writes nothing
            double e = 0;
            for (u32 i = 0; i < 128; ++i) e += double(wet[i]) * wet[i];
            if (b < 40) shut += e;
            else if (b < 100) { open += e; ++slices; if (e == 0) ++silenced; }
            else after += e;
        }
        if (shut != 0) fail("the space sounds with its gate shut");
        if (open == 0) fail("the space is silent with its gate open");
        if (after != 0) fail("the space isn't cut when its gate shuts");
        if (silenced < slices / 4 || silenced > slices * 3 / 4) fail("the space's stagger doesn't silence its slices");
    }
    // the sound, pinned
    {
        static const u32 expect[9] = {0xa3ce1fc5u, 0x72f8bbc5u, 0x74f66bc5u, 0x78769dc5u, 0x13830dc5u, 0xe70299c5u, 0xb3e0b5c5u,
                                      0xf00525c5u, 0x2b55adc5u};
        for (u32 a = 0; a < 9; ++a) {
            const u32 got = a < 6 ? pinned(a * 10 + (a * 3) % 10, false, (a & 1) == 0) : a == 6 ? pinned(50, true, true) :
                            a == 7 ? pinned(50, false, true, 1) : pinned(50, true, false, 1);
            // one key alone must have used its quiet blocks (or the pinned hash proves nothing about them); three never may
            if (a >= 7 && early_blocks == 0) fail("a lone key never worked the space out early");
            if (a == 8 && braindance::engine::touched == 0) fail("a lone key on a sample never read ahead through it");
            if (a < 7 && early_blocks > 12) fail("the space was worked out early with other keys sounding");
            if (got != expect[a]) { std::printf("FAIL the sound changed: render %u hashes to 0x%08x, not 0x%08x\n", a, got, expect[a]); ++failures; }
        }
    }
    // the short ways round a division
    {
        using namespace braindance::engine;
        u32 x = 1;
        for (u32 k = 0; k < 200000; ++k) {
            x = x * 1664525u + 1013904223u;
            const u32 v = k % 3 == 0 ? x : k % 3 == 1 ? x >> 8 : x >> 20;
            const u32 n = 1 + (x >> 3) % 40;
            if (quotient(v, n) != v / n || rest(v, n) != v % n) { fail("quotient or rest differs from the division"); break; }
            const i32 sv = static_cast<i32>(v) >> (k & 1), sn = static_cast<i32>(n) * ((k & 16) ? -1 : 1);
            if (divide(sv, sn) != sv / sn || remainder(sv, sn) != sv % sn) { fail("divide or remainder differs from the division"); break; }
        }
        for (u32 ms = 0; ms < 70000; ms += 7)
            if (decay(ms) != decay_of(ms) || decay(ms) != decay_of(ms)) { fail("a remembered decay differs"); break; }
    }
    if (!failures) std::puts("PASS brain dance sound");
    return failures ? 1 : 0;
}
