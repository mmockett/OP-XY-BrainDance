// Brain Dance's sound (README, "The sound" and "Samples"). Integer arithmetic only: audio
// in Q24 inside the voice (1.0 = 1 << 24), levels and coefficients in Q31,
// oscillators as 32-bit phases. The OP-XY has no divide instruction, so a
// block divides only when the tempo or the key changes or a tick has just
// passed: the clock's and a hit's answers are remembered, and small divisors
// go by their reciprocals. Each stage of the sound is a short loop of its own
// over the block (a lane's oscillator, then its level and pan; the space's
// lines one by one), with its settings in locals: the form the OP-XY runs
// fastest. The heaviest block is the one where a tick starts hits, so the
// quiet blocks just before a tick take what they can of its work: the tick
// is rehearsed, the sample its hits will play is read through, and a voice
// playing alone works the space's next block out early (render_sample).
// tests/sound.cpp pins the exact output, so changes to the speed can't
// change the sound.
#include "core.h"
#include "harvest.h"
#include <opxy/q15.hpp>
#include <opxy/q31.hpp>

namespace braindance {
namespace engine {
using opxy::q31::multiply;
using opxy::q15::product;

const u32 frames = opxy::block_frames;
const i32 one = 1 << 24;                 // Q24 full scale inside the voice
const i32 q31_one = 0x7fffffff;
const u32 hz = 97392u;                   // a phase increment per Hz, 2^32 / 44100

// ------------------------------------------------------------------ the clock
// One clock for every Brain Dance voice, in 32nd notes: the tick that last
// began and how far into the next one (Q32), at the start of the block the
// census is in. It follows the project's tempo, and while the sequencer plays
// it follows the song position, so patterns land on the OP-XY's grid. With
// the sequencer stopped it runs freely, and the first key held after silence
// restarts it, so a pattern starts on the key.
struct Clock {
    u32 cycle;        // the census: blocks rendered, counted by the first voice to render twice
    u32 count;        // voices rendered in this cycle so far
    u32 last;         // voices rendered in the last whole cycle: the keys held
    u32 tick, frac;   // the clock, 32nds and a fraction (Q32)
    u32 inc;          // its increment a sample at the tempo
    u32 playing;      // the sequencer played at the last block
    u32 last96;       // the song position it gave then
    // Kept between blocks so a block divides only when it must:
    u32 s32_inc, s32;                       // samples a 32nd, for this increment
    u32 until_frac, until_inc, until;       // samples to the next tick, at this fraction and increment
};
static Clock clock;

// A 32nd's increment a sample, Q32 of a 32nd: bpm_x10 x 2^32 / 3307500,
// without a division (2^32 / 3307500 = 1298.55398).
static inline u32 increment_of(u32 bpm_x10) {
    if (bpm_x10 < 200 || bpm_x10 > 3000) bpm_x10 = 1200;   // no tempo: 120 BPM, as Recur
    return bpm_x10 * 1298u + ((bpm_x10 * 36306u + 32768u) >> 16);
}

// The clock as a song position, in 96ths of a quarter (12 to a 32nd), Q16,
// and back. Positions are compared as differences, so they may wrap.
static inline u32 position96(u32 tick, u32 frac) { return tick * (12u << 16) + (frac >> 16) * 12u; }
static inline void place96(u32 at) {
    clock.tick = at / (12u << 16);
    const u32 rest = at % (12u << 16);
    clock.frac = rest * 5461u + rest / 3u;   // 2^32 / (12 << 16) = 5461.33
}
// While the sequencer plays, keep to its song position. It arrives once a
// block as a whole 96th, so the true position at the block's start lies in
// [at, at + 1). When it has just moved on, it moved on within the last block,
// which pins the true position to within a block: then the clock is held
// inside that window. On joining, the clock is put a block past the 96th: a
// sequenced note starts on a step, and the clock first follows a block later.
// A jump or a loop (more than a 96th out) puts it in the middle of the 96th.
static inline void follow(u32 at, u32 inc) {
    const u32 here = position96(clock.tick, clock.frac);
    const u32 low = at << 16;
    const i32 off = static_cast<i32>(here - low);
    // a block, in 96ths (Q16): 64 samples x inc (Q32 of a 32nd) x 12
    const u32 block = ((inc >> 12) * frames * 12u) >> 4;
    if (!clock.playing) {
        place96(low + (block < 65535u ? block : 65535u));
        return;
    }
    if (off < -65536 || off > 2 * 65536) {
        place96(low + 32768u);
        return;
    }
    if (off < 0) { place96(low); return; }
    const bool moved = at != clock.last96;
    if (moved && static_cast<u32>(off) > block) place96(low + block);
    else if (off > 65536) place96(low + 65536u);
}

// The first voice to render twice moves the census and the clock on a block.
static inline bool census(Voice& voice, const opxy::Transport& transport) {
    const bool leads = voice.cycle == clock.cycle;
    if (leads) {
        clock.last = clock.count;
        clock.count = 0;
        ++clock.cycle;
        const u32 before = clock.frac;
        clock.frac += clock.inc * frames;
        if (clock.frac < before) ++clock.tick;
        clock.inc = increment_of(transport.bpm_x10);
        if (transport.playing) follow(transport.ticks96, clock.inc);
        clock.last96 = transport.ticks96;
        clock.playing = transport.playing ? 1 : 0;
    }
    voice.cycle = clock.cycle;
    ++clock.count;
    return leads;
}

// ------------------------------------------------------------------ helpers
// Division and remainder by a small number, without dividing: the OP-XY has
// no divide instruction, and its software division takes a hundred cycles or
// more, where a tick may ask for several. x times n's reciprocal (2^32 / n,
// rounded up) is x / n or one more; the check puts it right. Exact for
// 0 <= x < 2^31 and 3 <= n <= 32; anything else is divided as before.
static const u32 reciprocal[33] = {0, 0, 0, 1431655766u, 1073741824u, 858993460u, 715827883u, 613566757u, 536870912u, 477218589u, 429496730u, 390451573u, 357913942u, 330382100u, 306783379u, 286331154u, 268435456u, 252645136u, 238609295u, 226050911u, 214748365u, 204522253u, 195225787u, 186737709u, 178956971u, 171798692u, 165191050u, 159072863u, 153391690u, 148102321u, 143165577u, 138547333u, 134217728u};
static inline u32 quotient_by(u32 x, u32 n, u32 over) {
    u32 q = static_cast<u32>(opxy::q31::wide(static_cast<i32>(x), static_cast<i32>(over)) >> 32);
    if (q * n > x) --q;
    return q;
}
__attribute__((noinline)) static u32 quotient(u32 x, u32 n) {
    return (n - 3u <= 29u && x < 0x80000000u) ? quotient_by(x, n, reciprocal[n]) : x / n;
}
__attribute__((noinline)) static u32 rest(u32 x, u32 n) {
    return (n - 3u <= 29u && x < 0x80000000u) ? x - quotient_by(x, n, reciprocal[n]) * n : x % n;
}
// The same for signed values, as C divides them: only what is not negative takes the short way.
static inline i32 divide(i32 x, i32 n) { return (x >= 0 && n > 0) ? static_cast<i32>(quotient(static_cast<u32>(x), static_cast<u32>(n))) : x / n; }
static inline i32 remainder(i32 x, i32 n) { return (x >= 0 && n > 0) ? static_cast<i32>(rest(static_cast<u32>(x), static_cast<u32>(n))) : x % n; }
static inline i32 clamp(i32 x, i32 low, i32 high) { return x < low ? low : x > high ? high : x; }
static inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
static inline u32 next(u32& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
static inline u32 scramble(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
// True with probability p (Q15).
static inline bool chance(Voice& v, i32 p) { return static_cast<i32>(next(v.random) >> 17) < p; }
// A signed random amount up to +-a (Q15).
static inline i32 jitter(Voice& v, i32 a) { return ((static_cast<i32>(next(v.random) >> 16) - 32768) * a) >> 15; }
static inline u32 below(Voice& v, u32 n) { return n ? rest(next(v.random) >> 8, n) : 0; }

// sin(2 pi phase), Q15: a parabola refined once (Nexus's).
// Every factor fits 16 bits, so each multiply is the OP-XY's one-cycle 16 x 16
// (a 32-bit multiply takes about three).
static inline i32 sine(u32 phase) {
    const i32 s = static_cast<i32>(phase) >> 16;
    const i32 a = s < 0 ? -s : s;
    const i32 p = product(s, 32767 - a) >> 13;
    const i32 pa = p < 0 ? -p : p;
    return p + (product((product(p, pa) >> 15) - p, 7373) >> 15);
}
// The key's increment moved by octaves into [low, 2 low): the lane keeps the key's pitch class.
static inline u32 fold(u32 inc, u32 low) {
    if (!inc) inc = low;
    while (inc >= 2 * low) inc >>= 1;
    while (inc < low) inc <<= 1;
    return inc;
}
// A level's fall a sample for a -60 dB decay over ms milliseconds: 1 - 6.9 / samples, Q31.
static inline i32 decay_of(u32 ms_x10) {
    const u32 scaled = ms_x10 * 441u;
    u32 samples = scaled < 0x80000000u ? quotient_by(scaled, 100u, 0xffffffffu / 100u + 1u) : scaled / 100u;
    if (samples < 32) samples = 32;
    return q31_one - static_cast<i32>((3704409293u / samples) << 2);
}
// The same, remembering the last answers: a hit asks for the few its controls
// give, over and over, and a division is slow on the OP-XY.
struct Fall { u32 key; i32 dec; };   // key: ms_x10 + 1 (0: empty)
static Fall falls[64];
static inline i32 decay(u32 ms_x10) {
    Fall& f = falls[(ms_x10 ^ (ms_x10 >> 6)) & 63u];
    if (f.key != ms_x10 + 1u) { f.key = ms_x10 + 1u; f.dec = decay_of(ms_x10); }
    return f.dec;
}
// Chamberlin's f = 2 sin(pi f / fs), about pi x inc, Q31, held below 0.6.
static inline i32 chamberlin(u32 inc) {
    const u32 f = inc * 3u + (inc >> 3) + (inc >> 6);
    return static_cast<i32>(f > 1288490188u ? 1288490188u : f);
}
// Pan gains for p (Q15, 16384 the middle).
static inline void pan(Lane& l, i32 p) {
    p = clamp(p, 0, 32767);
    l.left = (32767 - p) << 16;
    l.right = p << 16;
}

// ------------------------------------------------------------------ the space
// One gated, staggered reverb for every Brain Dance voice: a small feedback
// network of four delays that the voices' snap, click and blip lanes feed
// (bus), heard only while its gate is open. A snap opens the gate for a few
// steps; a low hit shuts it at once, so the noise fills the bass's rests and
// is cut dead when the bass returns; and while it's open a pattern of slices
// (the stagger, dealt at each snap, a triplet 64th or 128th long) lets it
// through or silences it, hard-edged. The voice that moves the census runs it,
// a block behind, and adds it to its own output.
// The network itself runs at half the sample rate, 22.05 kHz: each pair of
// sends is averaged into it, and what it gives back is drawn out to the full
// rate along straight lines. Its lines are damped to about 5 kHz every pass,
// so little of it lies above what the half rate carries, and it costs half as
// much. The gate and the stagger still cut it to the sample.
const u32 halves = frames / 2;   // the network's frames a block
struct Space {
    // Each line keeps its first 32 frames again past its end, so a block reads
    // its 32 frames in one straight run wherever they start.
    opxy::i16 line[4][1024 + halves];
    i32 damp[4];
    i32 bus[frames];   // the sends this cycle, Q24, at the full rate
    u32 at;            // the network's frames written: a whole number of blocks, so a block's writes never wrap
    i32 last[2];       // the last frame it gave back, each side: the next block's first straight line starts there
    u32 gate;          // samples the gate stays open
    u32 chop;          // the stagger: a slice sounds where its bit is set
    u32 slice, left;   // a slice's length, and the samples left of this one
    u32 bit;           // this slice's bit
    // Its next block, worked out early when a voice had time to spare (render_sample):
    // both sides, whether its gate opened at all, and whether it is waiting to be used.
    i32 early[2 * frames];
    u32 early_heard, early_ready;
};
static Space space;
static const u32 space_length[4] = {515, 663, 761, 899};   // the network's frames: 23 to 41 ms, no common factors

// One line's block: what it gave back (back), minus half of what all four gave
// (a lossless mix), at 0.85 a pass (about a second's tail, were it not gated),
// with the send, a little darker each pass. feed holds, for each frame, the
// send and that half already times 0.85 (Q15): what a line gave back fits 16
// bits, so its own multiply is the one-cycle 16 x 16, and the difference of
// the two products is the product of the difference, bit for bit.
__attribute__((noinline)) static void space_line(opxy::i16* write, const opxy::i16* back, const i32* feed, i32& damp) {
    i32 d = damp;
    for (u32 i = 0; i < halves; ++i) {
        const i32 v = feed[0] + (static_cast<i32>(static_cast<u32>(product(*back++, 27853)) - static_cast<u32>(feed[1])) >> 15);
        feed += 2;
        const i32 x = v - d;
        d += x - (x >> 2);   // three quarters of the way: the same cutoff as a half at the full rate
        *write++ = static_cast<opxy::i16>(clamp(d, -32767, 32767));
    }
    damp = d;
}

// What the four lines give back this block, as the two sides.
__attribute__((noinline)) static void space_sides(i32* sides, const opxy::i16* r0, const opxy::i16* r1, const opxy::i16* r2,
                                                  const opxy::i16* r3) {
    for (u32 i = 0; i < halves; ++i) {
        const i32 left = *r0++ + *r2++, right = *r1++ + *r3++;
        *sides++ = left;
        *sides++ = right;
    }
}

// What the network gave back, drawn out to the full rate: between each of its
// frames and the next, the point halfway. from: the frame before this block's first.
__attribute__((noinline)) static void space_draw(i32* out, const i32* sides, i32* from) {
    i32 left = from[0], right = from[1];
    for (u32 i = 0; i < halves; ++i) {
        const i32 l = sides[2 * i] << 11, r = sides[2 * i + 1] << 11;
        out[4 * i] = (left >> 1) + (l >> 1);
        out[4 * i + 1] = (right >> 1) + (r >> 1);
        out[4 * i + 2] = l;
        out[4 * i + 3] = r;
        left = l;
        right = r;
    }
    from[0] = left;
    from[1] = right;
}

// A block of the space into out, both sides. False if its gate was shut
// throughout: then out is left unwritten, and is silence.
__attribute__((noinline)) static bool space_run(i32* out) {
    Space& sp = space;
    const u32 w = sp.at & 1023u;
    i32 sides[2 * halves];   // lines 0 and 2 (left), lines 1 and 3 (right), as given back
    i32 feed[2 * halves];    // the send (Q14), and half of the four times 0.85 (Q15)
    space_sides(sides, sp.line[0] + ((w - space_length[0]) & 1023u), sp.line[1] + ((w - space_length[1]) & 1023u),
                sp.line[2] + ((w - space_length[2]) & 1023u), sp.line[3] + ((w - space_length[3]) & 1023u));
    for (u32 i = 0; i < halves; ++i) {
        feed[2 * i] = clamp(((sp.bus[2 * i] >> 1) + (sp.bus[2 * i + 1] >> 1)) >> 10, -16384, 16384);
        sp.bus[2 * i] = 0;
        sp.bus[2 * i + 1] = 0;
        feed[2 * i + 1] = ((sides[2 * i] + sides[2 * i + 1]) >> 1) * 27853;
    }
    // each line written out on its own, so each is a loop of its own
    space_line(sp.line[0] + w, sp.line[0] + ((w - space_length[0]) & 1023u), feed, sp.damp[0]);
    space_line(sp.line[1] + w, sp.line[1] + ((w - space_length[1]) & 1023u), feed, sp.damp[1]);
    space_line(sp.line[2] + w, sp.line[2] + ((w - space_length[2]) & 1023u), feed, sp.damp[2]);
    space_line(sp.line[3] + w, sp.line[3] + ((w - space_length[3]) & 1023u), feed, sp.damp[3]);
    if (w == 0)
        for (u32 k = 0; k < 4; ++k)
            for (u32 i = 0; i < halves; ++i) sp.line[k][1024 + i] = sp.line[k][i];
    sp.at += halves;
    if (!sp.gate) {
        // shut throughout: nothing to draw, but the next block's first line starts from this one's last frame
        sp.last[0] = sides[2 * halves - 2] << 11;
        sp.last[1] = sides[2 * halves - 1] << 11;
        return false;
    }
    space_draw(out, sides, sp.last);
    // The gate and its stagger, a stretch at a time: open, the slice's bit says whether it sounds.
    bool heard = false;
    for (u32 at = 0; at < frames;) {
        u32 n = frames - at;
        bool open = false;
        if (sp.gate) {
            if (sp.left == 0) { sp.left = sp.slice ? sp.slice : 1; sp.bit = (sp.bit + 1) & 31u; }
            if (sp.gate < n) n = sp.gate;
            if (sp.left < n) n = sp.left;
            sp.gate -= n;
            sp.left -= n;
            open = ((sp.chop >> sp.bit) & 1u) != 0;
        }
        if (open) heard = true;
        else for (u32 i = 2 * at; i < 2 * (at + n); ++i) out[i] = 0;
        at += n;
    }
    return heard;
}

// ------------------------------------------------------------------ what a block plays with
struct Shape {
    u32 algo, variant, key, s32;  // the algorithm and its variant (0..9), the note's increment, samples a 32nd
    i32 dense, chaos, glitch;     // with the keys held added (Q15)
    i32 tone, color, ring, crush; // Q15
    // the sample's snippets (harvest.h); count 0: none, the lanes play their synthesis
    const u32* rec;               // the first snippet's record
    u32 count;
    const opxy::i16* pcm;         // the sample
    u32 channels, hz;             // its channels and rate
    u32 rate;                     // its rate against 44.1 kHz, Q16
    u32 begin, end;               // the pad's trim: snippets inside it play
    u32 level;                    // the pad's gain, Q15
};

// Each role's lanes: how much of each lane a voice plays (Q15) and, for the
// polymeter, each lane's length in 16ths. The first key is the foundation;
// later keys play higher and busier.
static const i32 weight[4][lane_count] = {
    {32767, 29491, 22938, 9830}, {8192, 19661, 32767, 26214}, {4915, 16384, 32767, 32767}, {3277, 13107, 32767, 32767}};

// What a variant changes in every algorithm (its flavour): each lane's weight
// tilted by quarters, how a ratchet divides the 32nd (0 in halves, 1 in
// thirds, 2 in fifths), and whether the click (bit 0) and the blip (bit 1)
// play an octave up. Variant 0 is each algorithm as it was.
struct Flavour { opxy::i8 tilt[lane_count]; opxy::u8 burst, high; };
static const Flavour flavour[variant_count] = {
    {{0, 0, 0, 0}, 0, 0},  {{0, 1, 0, -1}, 1, 0},  {{-1, 0, 1, 1}, 0, 1},  {{1, -1, 1, 0}, 2, 0},  {{0, 1, -1, 1}, 1, 2},
    {{1, 0, 0, -2}, 0, 0}, {{-1, 1, 1, 0}, 1, 3},  {{0, -1, 2, 1}, 2, 1},  {{1, 1, -1, -1}, 0, 2}, {{-2, 1, 1, 2}, 1, 3}};
struct Shape;
static inline i32 weigh(const Shape& s, u32 rank, u32 lane);
static const i32 level[lane_count] = {1932735283, 1288490188, 966367641, 1073741824};   // 0.9, 0.6, 0.45, 0.5

static inline i32 weigh(const Shape& s, u32 rank, u32 lane) {
    const i32 w = weight[rank][lane];
    return clamp(w + (w >> 2) * flavour[s.variant].tilt[lane], 0, 32767);
}

// ------------------------------------------------------------------ the sample's snippets
// Each lane's choices, best first: its own kind of hit, played once; failing
// that, a loop its synthesis can shape (a tone in place of its oscillator, or
// noise in place of its noise); failing that, its synthesis.
static const u32 lane_hit[lane_count] = {harvest::kLowHit, harvest::kSnapHit, harvest::kClickHit, harvest::kBlipHit};
static const u32 lane_loop[lane_count] = {harvest::kToneLoop, harvest::kNoiseLoop, harvest::kNoiseLoop, harvest::kToneLoop};

// The snippets of a kind inside the pad's trim, best first.
static u32 usable(const Shape& s, u32 kind, const u32** out) {
    u32 n = 0;
    for (u32 i = 0; i < s.count && n < harvest::keep; ++i) {
        const u32* r = s.rec + 4 * i;
        if (harvest::kind_of(r) != kind) continue;
        const u32 start = harvest::start_of(r), length = harvest::length_of(r);
        if (start >= s.begin && start + length <= s.end) out[n++] = r;
    }
    return n;
}

// The key moved by octaves to within half an octave of middle C, as a ratio
// (Q16): a hit plays at its own pitch on C, and up to six semitones off it.
static inline u32 key_ratio(u32 inc) {
    const u32 c4 = 25480000u;                        // 261.63 Hz
    return fold(inc, c4 - (c4 >> 2) - (c4 >> 5) + (c4 >> 7)) / (c4 >> 16);   // from c4 / sqrt 2
}

static void take(Voice& v, u32 k, const Shape& s) {
    Lane& l = v.lane[k];
    l.src = 0;
    if (!s.count) return;
    const u32* found[harvest::keep];
    u32 kind = lane_hit[k], n = usable(s, kind, found);
    if (!n) { kind = lane_loop[k]; n = usable(s, kind, found); }
    if (!n) return;
    // chaos: now and then one of the others found
    const u32* r = found[n > 1 && chance(v, s.chaos) ? below(v, n) : 0];
    l.data = s.pcm + harvest::start_of(r) * s.channels;
    l.channels = s.channels;
    l.len = harvest::length_of(r);
    const u32 g = (harvest::gain_of(r) * (s.level >> 4)) >> 11;
    l.gain = static_cast<i32>(g > 65535 ? 65535 : g);
    if (kind == harvest::kToneLoop) {
        l.src = 2;
        l.pos = 0;
        l.cycles = harvest::cycles_of(r);
    } else if (kind == harvest::kNoiseLoop) {
        l.src = 3;
        l.pos = below(v, l.len) << 16;
        l.step = s.rate;
    } else {
        // once, tuned by the key; tone sets how much of it the level's fall lets through, 0.4 to 1.9 times its length
        l.src = 1;
        l.pos = 0;
        l.step = ((s.rate >> 4) * (key_ratio(s.key) >> 4)) >> 8;
        const u32 ms_x10 = l.len * 10000u / s.hz;
        l.dec = decay((ms_x10 * (13107u + (static_cast<u32>(s.tone) * 3u >> 1))) >> 15);
    }
}

// ------------------------------------------------------------------ a hit
static u32 strikes;   // hits since load, every voice: for the host tests
static void strike(Voice& v, u32 k, i32 vel, const Shape& s, bool gated) {
    ++strikes;
    Lane& l = v.lane[k];
    const i32 tone = s.tone, color = s.color;
    vel = multiply(vel, level[k]);
    l.amp = vel;
    l.cut = 0;
    switch (k) {
    case kLow: {
        if (!v.bend) {
            l.inc = fold(s.key, 50 * hz);                        // 50..100 Hz
            l.phase = 0;
            // drop: from up to seven times the pitch at tone 0 to about twice at 100
            const i32 drop = gated ? 1 : 2 + ((32767 - tone) * 5 >> 15);
            l.sweep = static_cast<i32>(l.inc) * drop;
            l.sweep_dec = decay(gated ? 40 : 150 + (tone * 250 >> 15));   // the drop over 4..40 ms
            l.dec = decay(gated ? 400 + (tone * 2000 >> 15) : 600 + ((tone >> 8) * (tone >> 8) * 7000 >> 14));
            l.amp2 = 0;
            if (gated) l.cut = static_cast<i32>(s.s32 >> 1) + static_cast<i32>((s.s32 * tone) >> 15);
        } else {
            // shred: a sweep down to the key's low octave from a chosen multiple of it, cut before the next 16th
            // (a hold lands an octave lower). It starts at its peak, so each step ticks, and
            // tone sets how much of the step it fills before the cut: 5/8 to all of it.
            l.inc = fold(s.key, (v.bend > 257 ? 46 : 40) * hz);   // 46..92 Hz, a hold 40..80
            l.phase = 0x40000000u;
            l.sweep = static_cast<i32>((l.inc >> 8) * static_cast<u32>(v.bend - 256));
            l.sweep_dec = decay(static_cast<u32>(v.bend_ms));
            l.dec = decay(3000 + (tone * 4000 >> 15));
            l.cut = static_cast<i32>(((2 * s.s32) * static_cast<u32>(20480 + (tone * 12288 >> 15))) >> 15);
            l.amp2 = color;                                  // how far its own sine bends it: from a sine to a buzz
            v.bend = 0;
        }
        space.gate = 0;                                      // the bass shuts the space
        break;
    }
    case kSnap: {
        const u32 centre = fold(s.key, 900 * hz) + ((fold(s.key, 900 * hz) >> 15) * color);   // 0.9..3.6 kHz
        l.f = chamberlin(centre);
        l.damp = 1288490188;                                 // 0.6: a broad band
        l.inc2 = fold(s.key, 160 * hz);                      // the body, 160..320 Hz
        l.amp2 = multiply(vel, (32767 - color) << 15);       // half at color 0, none at 100
        l.dec = decay(300 + (tone * 2200 >> 15));
        l.dec2 = decay(400 + (tone * 1200 >> 15));
        if (gated) { l.dec = q31_one; l.cut = static_cast<i32>(s.s32 >> 2) + static_cast<i32>((s.s32 * tone) >> 16); }
        l.w[0] = 0;                                          // the band alone (shred's blocks open it to the whole band)
        if (s.ring) {
            // the snap opens the space for 2 to 8 32nds and deals its stagger: glitch silences more slices
            const u32 hold = s.s32 * (2u + static_cast<u32>(s.ring * 6 >> 15));
            if (space.gate < hold) space.gate = hold;
            const u32 h = scramble(v.noise + static_cast<u32>(l.f));   // this voice's own draw, so a key always plays the same
            u32 chop = 0xffffffffu;
            for (u32 i = 0, six = 0, off = static_cast<u32>(s.glitch * 14 >> 15); i < off; ++i) {   // six: i % 6
                chop &= ~(1u << ((h >> (5 * six) ^ (i * 7u)) & 31u));
                if (++six == 6) six = 0;
            }
            space.chop = chop;
            space.slice = quotient(s.s32 * 2u, 3u) >> ((h >> 30) & 1u);
            space.left = 0;
        }
        break;
    }
    case kClick: {
        l.inc = fold(s.key, (gated ? 4000 : 2500) * hz) << (flavour[s.variant].high & 1u);   // 2.5..5 kHz, data 4..8 kHz; some variants an octave up
        l.phase = 0;
        l.inc2 = l.inc >> 1;
        // color: sine, square, then the bits (an Ikeda register's noise)
        const i32 c2 = color * 2;
        l.w[0] = clamp(32767 - c2, 0, 32767) << 16;
        l.w[1] = clamp(32767 - (c2 > 32767 ? c2 - 32767 : 32767 - c2), 0, 32767) << 15;
        l.w[2] = clamp(c2 - 32767, 0, 32767) << 15;
        l.dec = decay(20 + (tone * 400 >> 15));
        if (gated) { l.dec = q31_one; l.cut = static_cast<i32>(s.s32 >> 3) + static_cast<i32>((s.s32 * tone) >> 16); }
        break;
    }
    default: {   // kBlip
        l.inc = fold(s.key, (gated ? 1320 : 330) * hz) << ((flavour[s.variant].high >> 1) & 1u);   // 330..660 Hz, data 1.3..2.6 kHz; some variants an octave up
        l.phase = l.phase2 = 0;
        // the modulator at 1 to 7 times the carrier: whole numbers sing, between ring
        l.inc2 = (l.inc >> 8) * (256u + static_cast<u32>((color * 6 * 256) >> 15));
        l.amp2 = multiply(vel, 536870912 + (color << 15));   // the index, falling faster than the level
        l.dec = decay(200 + (tone * 3000 >> 15));
        l.dec2 = decay(100 + (tone * 800 >> 15));
        if (gated) { l.dec = q31_one; l.cut = static_cast<i32>(s.s32 >> 2); }
        break;
    }
    }
    take(v, k, s);
}

// A hit, perhaps with a ratchet: repeats more hits, every samples apart, each
// at decline times the last (Q15) and, for a roll, a little higher.
static void hit(Voice& v, u32 k, i32 vel, const Shape& s, i32 p, bool gated, i32 repeats, i32 every, i32 decline,
                bool rise) {
    strike(v, k, vel, s, gated);
    Lane& l = v.lane[k];
    pan(l, k == kLow ? 16384 : p);
    l.repeats = every >= 24 ? repeats : 0;
    l.every = every;
    l.wait = every;
    l.again = multiply(vel, decline << 16);
    l.rise = rise ? 1 : 0;
    l.climb = 0;
}

// The glitch: on a hit, sometimes a ratchet of 2 to 8, at a 32nd down to
// a 512th (an audio-rate buzz at the top).
static void ratchet(Voice& v, const Shape& s, i32 g, i32& repeats, i32& every) {
    repeats = 0;
    every = 0;
    if (!chance(v, g >> 1)) return;
    repeats = 1 + static_cast<i32>(below(v, 2 + (g * 6 >> 15)));
    u32 shift = below(v, 3);
    if (g > 22938 && chance(v, (g - 22938) * 3)) shift = 3 + below(v, 2);
    const u32 burst = flavour[s.variant].burst;
    every = static_cast<i32>((burst == 1 ? quotient(s.s32 * 2u, 3u) : burst == 2 ? quotient(s.s32 * 4u, 5u) : s.s32) >> shift);
}

// ------------------------------------------------------------------ the algorithms
static inline bool euclid(i32 k, i32 n, i32 pos) { return n > 0 && k > 0 && remainder(pos * k, n) < k; }
static inline i32 spread(Voice& v, i32 width) { return 16384 + jitter(v, width); }

// lanes: Autechre-style polymeter. Each lane is a euclidean rhythm of its own
// length; at each lane's downbeat chaos may nudge its rotation or its count,
// so the lanes drift against each other and never quite settle.
// Its lanes' lengths in 16ths: a variant's four for the first key, each later key's offset from them.
static const i32 lanes_base[variant_count][lane_count] = {
    {16, 12, 10, 7}, {16, 16, 12, 12}, {12, 9, 8, 5}, {20, 15, 12, 9}, {14, 10, 7, 5},
    {16, 11, 13, 6}, {18, 12, 9, 7},   {10, 10, 15, 6}, {24, 16, 9, 5}, {13, 11, 7, 3}};
static const i32 lanes_rank[4][lane_count] = {{0, 0, 0, 0}, {-5, -3, 3, -2}, {-1, -5, -1, 4}, {-3, -7, 7, -1}};
static inline i32 lanes_length(const Shape& s, u32 r, u32 k) {
    const i32 n = lanes_base[s.variant][k] + lanes_rank[r][k];
    return n < 3 ? 3 : n;
}
static void lanes(Voice& v, u32 t, const Shape& s) {
    const u32 r = v.rank;
    if (t & 1) {   // the odd 32nds: glitch's ghost clicks
        if (chance(v, (s.glitch * weigh(s, r, kClick) >> 15) >> 2)) hit(v, kClick, 644245094, s, spread(v, 16000), false, 0, 0, 0, false);
        return;
    }
    const i32 s16 = static_cast<i32>(t >> 1);
    for (u32 k = 0; k < lane_count; ++k) {
        const i32 n = lanes_length(s, r, k);
        i32 rot = remainder(v.rot[k], n);
        if (rot < 0) rot += n;
        const i32 pos = remainder(s16 + rot, n);
        if (pos == 0 && chance(v, s.chaos >> 1)) {
            v.rot[k] += (next(v.random) & 1) ? 1 : -1;
            v.extra[k] = clamp(v.extra[k] + ((next(v.random) & 1) ? 1 : -1), -2, 3);
        }
        const i32 hits = clamp(((s.dense * weigh(s, r, k) >> 15) * n * 3 >> 17) + v.extra[k] + (k == kLow ? 1 : 0), 0, n);
        if (!euclid(hits, n, pos)) continue;
        if (!chance(v, 32767 - (s.chaos * 11 >> 5))) continue;
        const i32 vel = clamp((pos == 0 ? 2040109465 : 1503238553) + (jitter(v, s.chaos >> 1) << 16), 214748364, q31_one);
        i32 repeats, every;
        ratchet(v, s, k == kLow ? s.glitch >> 2 : s.glitch, repeats, every);
        hit(v, k, vel, s, spread(v, 6000 + (s.chaos >> 1)), false, repeats, every, 27000, k == kBlip);
    }
}

// break: Aphex-style chopped breakbeat. A two-bar break (kick, snare, ride) is
// read a 16th at a time; chaos makes the reader jump to another 16th for a
// few steps, or stick on the last one (a stutter), and glitch rolls the snare
// up in pitch. Density adds ghost snares and the 32nd hats.
// Ten two-bar breaks: kick, snare and ride, a bit a 16th. 0 the amen; then a two-step, a second
// funk break, a half-time, a jungle chop, four on the floor broken, a 3-3-2, fives, a sparse one, a busy one.
static const u32 breaks[variant_count][3] = {
    {0x04050C05u, 0x42909290u, 0x55555555u}, {0x00410041u, 0x10101010u, 0xAAAAAAAAu}, {0x0C090405u, 0x92909290u, 0x55555555u},
    {0x04010001u, 0x01000100u, 0x11111111u}, {0x24012401u, 0x10901090u, 0x77777777u}, {0x11111111u, 0x40104010u, 0x44444444u},
    {0x49494949u, 0x10101010u, 0xB6B6B6B6u}, {0x42108421u, 0x08421084u, 0xFFFFFFFFu}, {0x00000401u, 0x10001000u, 0x01010101u},
    {0x0A05A405u, 0x52945290u, 0xDBDBDBDBu}};
static void breakbeat(Voice& v, u32 t, const Shape& s) {
    const u32 r = v.rank;
    const u32 break_kick = breaks[s.variant][0], break_snare = breaks[s.variant][1], break_ride = breaks[s.variant][2];
    if (t & 1) {
        if (r >= 2 || s.dense > 26214) {
            if (chance(v, ((s.dense - 13107) * weigh(s, r, kClick) >> 15))) hit(v, kClick, 858993459, s, spread(v, 12000), false, 0, 0, 0, false);
        }
        return;
    }
    const u32 s16 = t >> 1;
    if (v.jump_left > 0) --v.jump_left;
    else v.jump = 0;
    if (chance(v, divide(s.chaos * 2, 5))) {                       // chop: read elsewhere
        v.jump = static_cast<i32>(below(v, 16)) * 2;
        v.jump_left = 1 + static_cast<i32>(below(v, 4));
    } else if (chance(v, divide(s.chaos, 7))) {                    // stutter: hold the last 16th
        v.jump = -static_cast<i32>(((s16 - 1u) + 3u * r) & 31u) - 1;
        v.jump_left = 1 + static_cast<i32>(below(v, 3));
    }
    const u32 at = v.jump < 0 ? static_cast<u32>(-v.jump - 1) : (s16 + static_cast<u32>(v.jump) + 3u * r) & 31u;
    const u32 bit = 1u << at;
    if ((break_kick & bit) && chance(v, weigh(s, r, kLow) + 3000)) hit(v, kLow, 1932735283, s, 16384, false, 0, 0, 0, false);
    if (break_snare & bit) {
        if (chance(v, weigh(s, r, kSnap) + 4000)) {
            i32 repeats = 0, every = 0;
            if (chance(v, divide(s.glitch * 3, 5))) {             // the roll
                repeats = 2 + static_cast<i32>(below(v, 6));
                every = static_cast<i32>(s.s32 >> (1 + below(v, 2)));
            }
            hit(v, kSnap, 1825361100, s, spread(v, 5000), false, repeats, every, 33500, true);
        }
    } else if (chance(v, divide(s.dense * weigh(s, r, kSnap) >> 15, 3))) {
        hit(v, kSnap, 536870912, s, spread(v, 9000), false, 0, 0, 0, false);   // a ghost
    }
    if ((break_ride & bit) && s.dense > 6554 && chance(v, weigh(s, r, kClick))) {
        i32 repeats, every;
        ratchet(v, s, s.glitch >> 1, repeats, every);
        hit(v, kClick, (at & 3) ? 1073741824 : 1503238553, s, spread(v, 10000), false, repeats, every, 26000, false);
    }
    if ((break_snare & (bit << 3 | bit >> 29)) && chance(v, weigh(s, r, kBlip) >> 1))
        hit(v, kBlip, 1288490188, s, spread(v, 14000), false, 0, 0, 0, false);
}

// data: Ryoji Ikeda-style. A bar of 32nds drawn from a register, repeated
// exactly until chaos deals a new one at a bar's start; sine pulses on the
// beat, high beeps and bit noise under hard gates, hard left and right.
// A variant's bar in 32nds, its pulse's and its second pulse's periods, and where the snap falls in the pulse.
static const u32 data_shape[variant_count][4] = {
    {32, 8, 4, 4}, {24, 6, 3, 3}, {32, 16, 4, 12}, {20, 5, 5, 2}, {28, 7, 7, 3},
    {32, 12, 6, 6}, {16, 4, 2, 2}, {36, 9, 3, 4}, {32, 10, 5, 7}, {30, 6, 5, 3}};
static void data(Voice& v, u32 t, const Shape& s) {
    const u32* const d = data_shape[s.variant];
    const u32 r = v.rank, pos = rest(t, d[0]);
    if (pos == 0 && chance(v, s.chaos)) v.bits = next(v.random) | 1u;
    const u32 h = scramble(pos * 0x9e3779b9u ^ v.bits ^ (r << 24) ^ (s.variant * 0x51ed27u));
    if (rest(pos, d[1]) == 0 && chance(v, weigh(s, r, kLow))) hit(v, kLow, 1932735283, s, 16384, true, 0, 0, 0, false);
    else if (rest(pos, d[2]) == 0 && static_cast<i32>(h >> 17) < (s.dense * weigh(s, r, kLow) >> 16)) hit(v, kLow, 1288490188, s, 16384, true, 0, 0, 0, false);
    if (rest(pos, d[1]) == d[3] && static_cast<i32>((h >> 1) & 0x7fff) < (s.dense * weigh(s, r, kSnap) >> 15)) hit(v, kSnap, 1288490188, s, (h & 64) ? 2000 : 30767, true, 0, 0, 0, false);
    if (static_cast<i32>(h & 0x7fff) < (s.dense * weigh(s, r, kClick) >> 15) * 3 / 4) {
        i32 repeats, every;
        ratchet(v, s, s.glitch, repeats, every);
        hit(v, kClick, 1717986918, s, (pos & 1) ? 0 : 32767, true, repeats, every, 32767, false);
    }
    if (static_cast<i32>((h >> 9) & 0x7fff) < (s.dense * weigh(s, r, kBlip) >> 16)) hit(v, kBlip, 1288490188, s, (h & 128) ? 0 : 32767, true, 0, 0, 0, false);
}

// euclid: a cleaner skeleton. Every lane a euclidean rhythm on a 16-step bar
// (later keys 12 and 10, for threes against fours), each lane turned a little
// against the others; chaos only re-turns them now and then.
static const i32 euclid_length[variant_count][4] = {
    {16, 16, 12, 10}, {12, 12, 9, 8}, {16, 12, 10, 7}, {20, 16, 15, 12}, {14, 14, 10, 7},
    {16, 16, 16, 16}, {9, 12, 15, 6}, {24, 16, 12, 9}, {10, 15, 6, 9}, {13, 11, 7, 5}};
static const i32 euclid_turn[variant_count][lane_count] = {
    {0, 4, 2, 7}, {0, 3, 2, 5}, {0, 4, 5, 2}, {0, 5, 2, 8}, {0, 3, 6, 2},
    {0, 2, 1, 3}, {0, 4, 2, 1}, {0, 6, 3, 10}, {0, 2, 5, 3}, {0, 3, 1, 4}};
static void euclidean(Voice& v, u32 t, const Shape& s) {
    if (t & 1) return;
    const u32 r = v.rank;
    const i32 n = euclid_length[s.variant][r], s16 = static_cast<i32>(t >> 1), pos = remainder(s16, n);
    for (u32 k = 0; k < lane_count; ++k) {
        if (pos == 0 && chance(v, s.chaos >> 3)) v.rot[k] += 1 + static_cast<i32>(below(v, 3));
        const i32 p = remainder(pos + euclid_turn[s.variant][k] + v.rot[k] + static_cast<i32>(r) * 3, n);
        const i32 hits = clamp(((s.dense * weigh(s, r, k) >> 15) * n >> 16) + (k == kLow ? 2 : 1), 0, n);
        if (!euclid(hits, n, p)) continue;
        if (!chance(v, 32767 - (s.chaos >> 2))) continue;
        i32 repeats, every;
        ratchet(v, s, divide(s.glitch * 3, 5), repeats, every);
        hit(v, k, p == 0 ? 2040109465 : 1610612736, s, spread(v, 7000), false, repeats, every, 27000, false);
    }
}

// pulse: fire's network, on the grid. Four pacemaker neurons, one a lane, each
// counting 16ths to its own period and firing exactly on the step it reaches
// it; a firing nudges the next neuron in the ring a step or two forward, so
// the lanes lock into interlocking figures that repeat. Nothing is random at
// chaos 0: chaos only drops a pulse now and then, or pushes the next one a
// step early. Dense halves or doubles the periods; later keys take the periods
// in another order, so chords are polyrhythms; glitch adds ratchets.
// A variant's wiring: which neuron nudges each (the one this many places on), by how many steps, and its periods.
static const i32 pulse_shape[variant_count][3] = {
    {3, 1, 0}, {1, 1, 1}, {2, 2, 2}, {3, 1, 3}, {1, 2, 4}, {2, 1, 5}, {3, 2, 6}, {1, 1, 2}, {2, 2, 3}, {3, 1, 4}};
static const i32 pulse_period[7][lane_count] = {   // 16ths: low, snap, click, blip
    {4, 8, 3, 6}, {4, 8, 2, 5}, {6, 12, 3, 4}, {5, 10, 3, 7}, {3, 6, 2, 9}, {8, 8, 3, 5}, {4, 6, 2, 7}};
static void pulse(Voice& v, u32 t, const Shape& s) {
    if (t & 1) return;                                       // on the 16ths only
    const u32 r = v.rank, s16 = t >> 1;
    const i32* const f = pulse_shape[s.variant];
    // each lane's period: the variant's, in another order for each later key, halved or doubled by density
    i32 period[lane_count];
    for (u32 k = 0; k < lane_count; ++k) {
        const i32 base = pulse_period[f[2]][(k + r) & 3u];
        const i32 d = s.dense * weigh(s, r, k) >> 15;
        period[k] = d < 8192 ? base * 2 : d < 20000 ? base : (base + 1) / 2;
    }
    if (!(v.spikes & 0x80000000u)) {
        // the first step: the low fires at once, the snap half a period later, the click at once, the blip two steps on
        v.potential[kLow] = period[kLow] - 1;
        v.potential[kSnap] = period[kSnap] / 2 - 1;
        v.potential[kClick] = period[kClick] - 1;
        v.potential[kBlip] = period[kBlip] - 3;
        v.spikes = 0x80000000u;
    }
    u32 now = 0;
    for (u32 k = 0; k < lane_count; ++k) {
        const bool nudged = (v.spikes & (1u << ((k + static_cast<u32>(f[0])) & 3u))) != 0;
        i32 u = v.potential[k] + 1 + (nudged ? f[1] : 0);
        if (u >= period[k]) {
            u -= period[k];
            if (u >= period[k]) u = period[k] - 1;
            now |= 1u << k;
            if (chance(v, s.chaos >> 3)) ++u;                 // chaos: the next one a step early
            if (!chance(v, s.chaos >> 2)) {                   // chaos: or this one dropped
                i32 repeats, every;
                ratchet(v, s, k == kLow ? s.glitch >> 2 : s.glitch, repeats, every);
                // accented on the bar's beats, and when a neighbour set it off
                hit(v, k, nudged || (s16 & 7u) == 0 ? 2040109465 : 1503238553, s, spread(v, 6000), false, repeats, every, 29000, k == kBlip);
            }
        }
        v.potential[k] = u;
    }
    v.spikes = now | 0x80000000u;
}

// shred: frantic and polyrhythmic. The bass sweeps down to the key on nearly
// every 16th, from a different height each step of its cycle; the cycles are
// six steps against the bar's eight for the first key, then eight, five and
// seven for the next, so held keys grind against each other. Now and then, at
// a cycle's start, the bass rests a few steps under a block of gated noise
// that falls slowly and is cut dead (and the space with it); or it lands and
// holds. Hard-gated snaps on the backbeat open the space; clicks fall between
// the 16ths, in triplet stutters with glitch; and a high beep marks the
// cycle's middle.
static const u32 shred_cycle[variant_count][4] = {
    {6, 8, 5, 7}, {5, 7, 6, 9}, {7, 8, 5, 6}, {3, 4, 5, 7}, {8, 6, 7, 5},
    {9, 8, 7, 6}, {4, 6, 5, 3}, {10, 8, 6, 7}, {6, 5, 4, 7}, {12, 9, 8, 5}};
// Three shapes of sweep (a zigzag, a climb, wide leaps), as multiples of the landing pitch (Q8); a variant picks one and where it starts.
static const i32 shred_start[3][8] = {
    {640, 512, 768, 512, 896, 576, 704, 448}, {448, 512, 576, 640, 704, 768, 896, 1024}, {1024, 384, 896, 448, 1280, 512, 768, 320}};
static const i32 shred_block[variant_count] = {4096, 4096, 6000, 3000, 8000, 2000, 5000, 4096, 3000, 9000};   // how often a block (Q15)
static const u32 shred_hold[variant_count] = {3, 3, 4, 3, 2, 3, 4, 2, 1, 3};                                   // how often a hold: chaos shifted down by this
static void shred(Voice& v, u32 t, const Shape& s) {
    const u32 r = v.rank;
    if (t & 1) {
        if (chance(v, (s.glitch * weigh(s, r, kClick) >> 15) >> 1)) {
            i32 repeats = 0, every = 0;
            if (chance(v, s.glitch)) {
                repeats = 1 + static_cast<i32>(below(v, 4));
                every = static_cast<i32>(quotient(s.s32 * 2u, 3u) >> below(v, 2));
            }
            hit(v, kClick, 1073741824, s, (next(v.random) & 1u) ? 3000 : 29767, true, repeats, every, 31000, false);
        }
        return;
    }
    const u32 s16 = t >> 1, n = shred_cycle[s.variant][r], pos = rest(s16, n);
    // rest: above 0, steps left of a block of noise (nothing may cut it); below 0, steps left of a held bass note
    const bool block = v.rest > 0;
    if (v.rest > 0) {
        --v.rest;
    } else if (v.rest < 0) {
        ++v.rest;
    } else if (pos == 0 && chance(v, (shred_block[s.variant] + (s.chaos >> 1)) >> r)) {   // later keys, fewer: the first owns the blocks
        // a block: two to four steps of noise falling slowly, cut dead, the space open with it
        v.rest = 1 + static_cast<i32>(below(v, 3));
        hit(v, kSnap, 1932735283, s, spread(v, 3000), false, 0, 0, 0, false);
        Lane& l = v.lane[kSnap];
        if (l.src != 1) { l.dec = decay(6000); l.amp2 = 0; l.w[0] = q31_one >> 1; }   // most of the band, not the snap's alone
        l.cut = static_cast<i32>(2 * s.s32) * (v.rest + 1);
        if (s.ring) space.gate = static_cast<u32>(l.cut);
    } else if (chance(v, ((14746 + (s.dense * 9 >> 4)) * (16384 + (weigh(s, r, kLow) >> 1))) >> 15)) {
        const bool hold = chance(v, s.chaos >> shred_hold[s.variant]);   // it lands and holds, with no sweep
        v.bend = hold ? 257 : shred_start[s.variant % 3][(pos + s.variant + (chance(v, s.chaos >> 1) ? below(v, 8) : 0)) & 7u];
        v.bend_ms = 2200 + (s.tone * 2000 >> 15);   // the sweep still falling as the step ends
        hit(v, kLow, pos == 0 ? 2040109465 : 1610612736, s, 16384, false, 0, 0, 0, false);
        if (hold) {
            v.rest = -1 - static_cast<i32>(below(v, 2));
            v.lane[kLow].cut = static_cast<i32>(2 * s.s32) * (1 - v.rest);
        }
    }
    if (block || v.rest > 0) return;                         // the block has the snap lane, and the floor
    const bool gap = v.lane[kLow].amp <= 0 || v.lane[kLow].cut <= 0;   // the bass skipped this step
    if (gap && v.rest == 0 && r == 0) {
        // no dead air: a skipped step takes a gated snap, which opens the space
        hit(v, kSnap, 1288490188, s, spread(v, 9000), true, 0, 0, 0, false);
        v.lane[kSnap].cut = static_cast<i32>(s.s32 + (s.s32 >> 1));
    } else if ((s16 & 7u) == 4 && chance(v, weigh(s, r, kSnap) + 3000)) {
        hit(v, kSnap, 1503238553, s, spread(v, 5000), true, 0, 0, 0, false);
    } else if (chance(v, (s.dense * weigh(s, r, kClick) >> 15) >> 1)) {
        i32 repeats, every;
        ratchet(v, s, s.glitch, repeats, every);
        hit(v, kClick, 858993459, s, spread(v, 14000), true, repeats, every, 30000, false);
    }
    if (pos == n / 2 && chance(v, weigh(s, r, kBlip))) hit(v, kBlip, 1073741824, s, (s16 & 1u) ? 2000 : 30767, true, 0, 0, 0, false);
}

static void tick(Voice& v, u32 t, const Shape& s) {
    // glitch: now and then a dropout, a millisecond or two of dead silence
    if (s.glitch > 9830 && chance(v, (s.glitch - 9830) >> 3)) v.drop = 44 + below(v, 66);
    switch (s.algo) {
    case kLanes: lanes(v, t, s); break;
    case kBreak: breakbeat(v, t, s); break;
    case kData: data(v, t, s); break;
    case kEuclid: euclidean(v, t, s); break;
    case kPulse: pulse(v, t, s); break;
    default: shred(v, t, s); break;
    }
}

// A tick's code and tables have gone cold by the time it comes round again,
// and on the OP-XY a tick with hits then costs several times its work, in the
// very block that has the most to do. So when a voice has quiet blocks to
// spare shortly before a tick, it plays that tick on a copy of itself (the
// rehearsal), and a block later reads through the stretches of the sample the
// copy's hits would play first, so those are to hand as well: a sample read
// for the first time costs about a hundred cycles every 32 bytes. Nothing the
// rehearsal does is kept, so the sound is the same without it.
struct Rehearsal {
    Voice copy;        // the voice after the tick
    const Voice* of;   // whose rehearsal this is, and of which tick
    u32 tick;
};
static Rehearsal rehearsal;
u32 touched;           // what warm read, summed (so its reads are kept; the tests read it)

__attribute__((noinline)) static void rehearse(const Voice& voice, u32 t, const Shape& s) {
    rehearsal.copy = voice;
    const u32 gate = space.gate, chop = space.chop, slice = space.slice, left = space.left, bit = space.bit;
    const u32 struck = strikes;
    tick(rehearsal.copy, t, s);
    space.gate = gate;
    space.chop = chop;
    space.slice = slice;
    space.left = left;
    space.bit = bit;
    strikes = struck;
    rehearsal.of = &voice;
    rehearsal.tick = t;
}

// Reads through what each of a rehearsed voice's lanes would play of the
// sample in its first two blocks, a frame every 32 bytes: at most 1024 frames a lane.
__attribute__((noinline)) static void warm(const Voice& v) {
    u32 sum = 0;
    for (u32 k = 0; k < lane_count; ++k) {
        const Lane& l = v.lane[k];
        if (l.amp <= 0 || !l.src || !l.len || l.len > 65536u) continue;
        u32 first, count;   // frames
        if (l.src == 2) {   // a tone loop: its phase moves by the pitch and the sweep, at most
            const u32 step = (l.inc + static_cast<u32>(l.sweep < 0 ? -l.sweep : l.sweep)) >> l.cycles;
            const u32 span = step > 0xffffffffu / (2 * frames) ? 0xffffffffu : step * (2 * frames);
            first = ((l.pos >> 16) * l.len) >> 16;
            count = (((span >> 16) * l.len) >> 16) + 2;
        } else {            // a hit or a noise loop: its position moves by its step
            first = l.pos >> 16;
            count = (((l.step >> 8) * (2 * frames)) >> 8) + 2;
        }
        if (first >= l.len) continue;
        if (count > 1024) count = 1024;
        if (count > l.len - first) count = l.len - first;
        const opxy::i16* d = l.data + first * l.channels;
        const u32 words = count * l.channels;
        for (u32 i = 0; i < words; i += 16) sum += static_cast<u32>(d[i]);
    }
    touched += sum;
}

// ------------------------------------------------------------------ the lanes' sound, a stretch of samples at a time
// Each adds its lane into the mix (Q24), left and right interleaved. A lane's
// settings are read into locals for the stretch and written back after it: the
// loops then hold them in registers, where the OP-XY is several times faster.
// set: the lane is the first to write this stretch of the buffer, so it writes
// its samples outright, and the caller clears what is left of the stretch;
// otherwise it adds to what is there. Each returns how many samples it wrote.
struct Run { i32* mix; u32 n; bool set; };

static inline bool gate(Lane& l) {   // after a sample: the hard gate, or the level's fall
    if (l.cut > 0 && --l.cut == 0) { l.amp = 0; return false; }
    l.amp = multiply(l.amp, l.dec);
    return true;
}
// How many of a stretch's n samples a lane sounds before its hard gate shuts
// it: all of them, or the cut's last (ends, and its level is 0 after them).
static inline u32 sounding(Lane& l, u32 n, bool& ends) {
    ends = false;
    if (l.cut > 0) {
        if (static_cast<u32>(l.cut) <= n) { n = static_cast<u32>(l.cut); ends = true; }
        l.cut -= static_cast<i32>(n);
    }
    return n;
}

// A snippet of the sample: its frames, and its gain to the lane's level (Q12).
struct Clip { const opxy::i16* data; u32 len; u32 channels; i32 gain; };
static inline Clip clip_of(const Lane& l) { const Clip c = {l.data, l.len, l.channels, l.gain}; return c; }
// A frame (both sides of a stereo sample, averaged), and at a Q16 position,
// interpolated, at its gain: Q24.
static inline i32 clip_at(const Clip& c, u32 i) {
    const opxy::i16* d = c.data;
    return c.channels == 2 ? (static_cast<i32>(d[2 * i]) + d[2 * i + 1]) >> 1 : d[i];
}
static inline i32 clip_read(const Clip& c, u32 pos, bool wrap) {
    u32 i = pos >> 16;
    if (i >= c.len) i = c.len - 1;
    u32 j = i + 1;
    if (j >= c.len) j = wrap ? 0 : i;
    const i32 a = clip_at(c, i), b = clip_at(c, j);
    const i32 x = a + (((b - a) * static_cast<i32>((pos >> 1) & 0x7fffu)) >> 15);
    return (x * c.gain) >> 3;
}
// A tone loop at its phase (Q32 of the whole loop).
static inline i32 clip_tone(const Clip& c, u32 phase) { return clip_read(c, (phase >> 16) * c.len, true); }
// The same for a loop of one channel and at most 65536 frames, as nearly all
// are: the position can't pass the loop's end, so there is nothing to check.
static inline i32 clip_tone_plain(const Clip& c, u32 phase) {
    const u32 pos = (phase >> 16) * c.len;
    const u32 i = pos >> 16;
    u32 j = i + 1;
    if (j >= c.len) j = 0;
    const i32 a = c.data[i], b = c.data[j];
    const i32 x = a + (((b - a) * static_cast<i32>((pos >> 1) & 0x7fffu)) >> 15);
    return (x * c.gain) >> 3;
}
static inline bool plain(const Clip& c) { return c.channels != 2 && c.len <= 65536u; }
// A noise loop's next value.
static inline i32 clip_noise(const Clip& c, u32& pos, u32 step) {
    const i32 x = clip_read(c, pos, true);
    pos += step;
    if (pos >= c.len << 16) pos -= c.len << 16;
    return x;
}

// Any lane playing a hit from the sample: once, to its end or the level's fall.
__attribute__((noinline)) static u32 run_shot(Lane& l, Run r) {
    const u32 end = (l.len - 1) << 16;
    const Clip c = clip_of(l);
    u32 i = 0;
    for (; i < r.n; ++i) {
        if (l.pos >= end) { l.amp = 0; break; }
        const i32 y = multiply(clip_read(c, l.pos, false), l.amp);
        l.pos += l.step;
        if (r.set) {
            r.mix[2 * i] = multiply(y, l.left);
            r.mix[2 * i + 1] = multiply(y, l.right);
        } else {
            r.mix[2 * i] += multiply(y, l.left);
            r.mix[2 * i + 1] += multiply(y, l.right);
        }
        if (!gate(l)) { ++i; break; }
    }
    return i;
}

// A lane's stretch (dry, Q24, before its level) into the mix at its level and
// pan, the level falling a sample at a time. Each lane works its stretch out
// into dry first and hands it here: short loops with few values live, which
// the OP-XY keeps in its data registers.
__attribute__((noinline)) static i32 mix_in(i32* mix, const i32* dry, u32 n, i32 amp, i32 dec, i32 left, i32 right, bool set) {
    if (right == 0x40000000) {
        // The middle (the low lane's): the right side's gain is a half exactly, and a half is a shift.
        if (set) {
            for (u32 i = 0; i < n; ++i) {
                const i32 y = multiply(dry[i], amp);
                mix[2 * i] = multiply(y, left);
                mix[2 * i + 1] = y >> 1;
                amp = multiply(amp, dec);
            }
        } else {
            for (u32 i = 0; i < n; ++i) {
                const i32 y = multiply(dry[i], amp);
                mix[2 * i] += multiply(y, left);
                mix[2 * i + 1] += y >> 1;
                amp = multiply(amp, dec);
            }
        }
    } else if (set) {
        for (u32 i = 0; i < n; ++i) {
            const i32 y = multiply(dry[i], amp);
            mix[2 * i] = multiply(y, left);
            mix[2 * i + 1] = multiply(y, right);
            amp = multiply(amp, dec);
        }
    } else {
        for (u32 i = 0; i < n; ++i) {
            const i32 y = multiply(dry[i], amp);
            mix[2 * i] += multiply(y, left);
            mix[2 * i + 1] += multiply(y, right);
            amp = multiply(amp, dec);
        }
    }
    return amp;
}
// The same with a second part already at its own level (the snap's body).
__attribute__((noinline)) static i32 mix_in_with(i32* mix, const i32* dry, const i32* body, u32 n, i32 amp, i32 dec, i32 left,
                                                 i32 right, bool set) {
    if (set) {
        for (u32 i = 0; i < n; ++i) {
            const i32 y = multiply(dry[i], amp) + body[i];
            mix[2 * i] = multiply(y, left);
            mix[2 * i + 1] = multiply(y, right);
            amp = multiply(amp, dec);
        }
    } else {
        for (u32 i = 0; i < n; ++i) {
            const i32 y = multiply(dry[i], amp) + body[i];
            mix[2 * i] += multiply(y, left);
            mix[2 * i + 1] += multiply(y, right);
            amp = multiply(amp, dec);
        }
    }
    return amp;
}

__attribute__((noinline)) static u32 run_low(Lane& l, Run r) {
    bool ends;
    const u32 n = sounding(l, r.n, ends);
    i32 dry[frames];
    u32 at[frames];   // the oscillator's phase, or the loop's, each sample
    {
        const i32 sweep_dec = l.sweep_dec;
        const u32 inc = l.inc;
        i32 sweep = l.sweep;
        if (l.src == 2) {   // a tone loop in place of the sine, with the same drop
            const u32 cycles = l.cycles;
            u32 pos = l.pos;
            for (u32 i = 0; i < n; ++i) {
                sweep = multiply(sweep, sweep_dec);
                pos += (inc + static_cast<u32>(sweep)) >> cycles;
                at[i] = pos;
            }
            l.pos = pos;
        } else {
            u32 phase = l.phase;
            for (u32 i = 0; i < n; ++i) {
                sweep = multiply(sweep, sweep_dec);
                phase += inc + static_cast<u32>(sweep);
                at[i] = phase;
            }
            l.phase = phase;
        }
        l.sweep = sweep;
    }
    if (l.src == 2) {
        const Clip c = clip_of(l);
        if (plain(c)) for (u32 i = 0; i < n; ++i) dry[i] = clip_tone_plain(c, at[i]);
        else for (u32 i = 0; i < n; ++i) dry[i] = clip_tone(c, at[i]);
    } else if (l.amp2) {
        const i32 bend = l.amp2;   // shred's sweeps bend their own phase by color (Q15), up to about a radian and a half
        for (u32 i = 0; i < n; ++i) {
            const u32 bent = at[i] + static_cast<u32>(product(sine(at[i]), bend));   // Q30: a quarter cycle at most
            dry[i] = sine(bent) << 9;
        }
    } else {
        for (u32 i = 0; i < n; ++i) dry[i] = sine(at[i]) << 9;
    }
    const i32 amp = mix_in(r.mix, dry, n, l.amp, l.dec, l.left, l.right, r.set);
    l.amp = ends ? 0 : amp;
    return n;
}

__attribute__((noinline)) static u32 run_snap(Lane& l, u32& noise_state, Run r) {
    bool ends;
    const u32 n = sounding(l, r.n, ends);
    i32 dry[frames], body[frames];
    // the noise: the sample's, or the lane's own
    if (l.src == 3) {
        const Clip c = clip_of(l);
        const u32 step = l.step;
        u32 pos = l.pos;
        for (u32 i = 0; i < n; ++i) dry[i] = clip_noise(c, pos, step);
        l.pos = pos;
    } else {
        u32 noise = noise_state;
        for (u32 i = 0; i < n; ++i) {
            noise = noise * 1664525u + 1013904223u;
            dry[i] = static_cast<i32>(noise) >> 8;
        }
        noise_state = noise;
    }
    // through its band-pass, with as much of the highs as w[0] lets past
    {
        const i32 f = l.f, damp = l.damp, highs = l.w[0];
        i32 low = l.s1, band = l.s2;
        for (u32 i = 0; i < n; ++i) {
            low = opxy::q31::add(low, multiply(f, band));   // (saturating adds stay in the data registers)
            const i32 high = dry[i] - low - multiply(damp, band);
            band += multiply(f, high);
            dry[i] = opxy::q31::add(band << 1, multiply(high, highs));
        }
        l.s1 = ends ? 0 : low;
        l.s2 = ends ? 0 : band;
    }
    // the body, at its own falling level; once that is nothing, only its phase moves on
    if (!l.amp2) {
        l.phase2 += l.inc2 * n;
        const i32 amp = mix_in(r.mix, dry, n, l.amp, l.dec, l.left, l.right, r.set);
        l.amp = ends ? 0 : amp;
        return n;
    }
    {
        const u32 inc2 = l.inc2;
        const i32 dec2 = l.dec2;
        u32 phase2 = l.phase2;
        i32 amp2 = l.amp2;
        for (u32 i = 0; i < n; ++i) {
            phase2 += inc2;
            body[i] = multiply(sine(phase2) << 9, amp2);
            amp2 = multiply(amp2, dec2);
        }
        l.phase2 = phase2;
        l.amp2 = amp2;
    }
    const i32 amp = mix_in_with(r.mix, dry, body, n, l.amp, l.dec, l.left, l.right, r.set);
    l.amp = ends ? 0 : amp;
    return n;
}

__attribute__((noinline)) static u32 run_click(Lane& l, u32& noise_state, Run r) {
    bool ends;
    const u32 n = sounding(l, r.n, ends);
    i32 dry[frames];
    if (l.src == 3) {   // the sample's noise in place of the tone and the bits
        const Clip c = clip_of(l);
        const u32 step = l.step;
        u32 pos = l.pos;
        for (u32 i = 0; i < n; ++i) dry[i] = clip_noise(c, pos, step);
        l.pos = pos;
    } else {
        {   // the tone: a sine and a square, each at its weight
            const u32 inc = l.inc;
            const i32 sines = l.w[0], up = multiply(one, l.w[1]), down = multiply(-one, l.w[1]);
            u32 phase = l.phase;
            for (u32 i = 0; i < n; ++i) {
                phase += inc;
                dry[i] = multiply(sine(phase) << 9, sines) + ((phase & 0x80000000u) ? down : up);
            }
            l.phase = phase;
        }
        {   // the bits: the register clocks a new one each time its phase wraps
            const u32 inc2 = l.inc2;
            const i32 bits = l.w[2];
            u32 phase2 = l.phase2, noise = noise_state;
            i32 held = l.held, bit = multiply(held, bits);
            for (u32 i = 0; i < n; ++i) {
                const u32 before = phase2;
                phase2 += inc2;
                if (phase2 < before) {
                    noise = noise * 1664525u + 1013904223u;
                    held = (noise & 0x80000000u) ? one : -one;
                    bit = multiply(held, bits);
                }
                dry[i] += bit;
            }
            l.phase2 = phase2;
            l.held = held;
            noise_state = noise;
        }
    }
    const i32 amp = mix_in(r.mix, dry, n, l.amp, l.dec, l.left, l.right, r.set);
    l.amp = ends ? 0 : amp;
    return n;
}

__attribute__((noinline)) static u32 run_blip(Lane& l, Run r) {
    bool ends;
    const u32 n = sounding(l, r.n, ends);
    i32 dry[frames];
    u32 bend[frames];   // how far the modulator bends the carrier's phase, each sample
    dry[0] = 0;         // (a stretch has at least a sample; this lets every compiler see dry set)
    {
        const u32 inc2 = l.inc2;
        const i32 dec2 = l.dec2;
        u32 phase2 = l.phase2;
        i32 amp2 = l.amp2;
        for (u32 i = 0; i < n; ++i) {
            phase2 += inc2;
            bend[i] = static_cast<u32>(product(sine(phase2), amp2 >> 16)) << 2;   // the index, Q15: up to about pi
            amp2 = multiply(amp2, dec2);
        }
        l.phase2 = phase2;
        l.amp2 = amp2;
    }
    const u32 inc = l.inc;
    if (l.src == 2) {   // a tone loop as the carrier: the modulator bends where it reads
        const Clip c = clip_of(l);
        const u32 cycles = l.cycles;
        u32 pos = l.pos;
        for (u32 i = 0; i < n; ++i) {
            pos += inc >> cycles;
            dry[i] = clip_tone(c, pos + (bend[i] >> cycles));
        }
        l.pos = pos;
        l.phase += inc * n;
    } else {
        u32 phase = l.phase;
        for (u32 i = 0; i < n; ++i) {
            phase += inc;
            dry[i] = sine(phase + bend[i]) << 9;
        }
        l.phase = phase;
    }
    const i32 amp = mix_in(r.mix, dry, n, l.amp, l.dec, l.left, l.right, r.set);
    l.amp = ends ? 0 : amp;
    return n;
}

// A stretch of the block: the low lane into the mix; the others into aux, which
// joins the mix and feeds the space. The low lane is the mix's only writer here,
// so it writes it outright, and whatever of the stretch it leaves is cleared;
// the same for the first of the others to sound into aux. used: aux is in use
// this block, so a stretch with none of them sounding is cleared too.
static u32 run_lane(Voice& v, u32 k, Run r) {
    Lane& l = v.lane[k];
    if (l.src == 1) return run_shot(l, r);
    switch (k) {
    case kLow: return run_low(l, r);
    case kSnap: return run_snap(l, v.noise, r);
    case kClick: return run_click(l, v.noise, r);
    default: return run_blip(l, r);
    }
}
static void run(Voice& v, i32* mix, i32* aux, u32 n, bool used) {
    {
        const Run r = {mix, n, true};
        const u32 wrote = v.lane[kLow].amp > 0 ? run_lane(v, kLow, r) : 0;
        for (u32 i = 2 * wrote; i < 2 * n; ++i) mix[i] = 0;
    }
    bool first = true;
    for (u32 k = kSnap; k < lane_count; ++k) {
        if (v.lane[k].amp <= 0) continue;
        const Run r = {aux, n, first};
        const u32 wrote = run_lane(v, k, r);
        if (first) {
            for (u32 i = 2 * wrote; i < 2 * n; ++i) aux[i] = 0;
            first = false;
        }
    }
    if (first && used)
        for (u32 i = 0; i < 2 * n; ++i) aux[i] = 0;
}

// A lane's next ratchet hit.
static void repeat(Voice& v, u32 k, const Shape& s) {
    Lane& l = v.lane[k];
    const i32 again = l.again, every = l.every, repeats = l.repeats - 1, rise = l.rise;
    const i32 left = l.left, right = l.right;
    const bool gated = s.algo == kData;
    const i32 climb = l.climb + 1;
    strike(v, k, again, s, gated);
    if (rise && k != kLow) {                     // a roll climbs about a semitone a hit
        l.inc += (l.inc >> 4) * static_cast<u32>(climb);
        l.inc2 += (l.inc2 >> 4) * static_cast<u32>(climb);
        if (k == kSnap) l.f = chamberlin(static_cast<u32>(l.f) / 3u + (static_cast<u32>(l.f) / 48u) * static_cast<u32>(climb));
        if (l.src == 1 || l.src == 3) l.step += (l.step >> 4) * static_cast<u32>(climb);
    }
    l.climb = climb;
    l.left = left;
    l.right = right;
    l.repeats = repeats;
    l.every = every;
    l.wait = every;
    l.again = multiply(again, 31000 << 16);
    l.rise = rise;
}

// ------------------------------------------------------------------ after the lanes
// ring, above half: a comb at the key (a delay of one cycle, fed back), on both
// sides alike: a metallic buzz with every harmonic. crush: drive into a hard
// clip, and above 60% fewer bits and a lower sample rate as well.
__attribute__((noinline)) static void comb_block(i32* mix, Voice& v, u32 delay, i32 feedback, i32 wet) {
    // comb_at is a whole number of blocks, so a block writes one straight run:
    // frames 0..63 or 64..127. The first 64 are kept again past the end (written
    // there too as they are written), so the read, delay behind, is one straight
    // run as well.
    const u32 at = v.comb_at & 127u;
    opxy::i16 nowhere[frames];
    opxy::i16* write = v.comb + at;
    opxy::i16* again = at == 0 ? v.comb + 128 : nowhere;
    const opxy::i16* read = v.comb + ((at - delay) & 127u);
    i32 low = v.comb_low;
    for (u32 i = 0; i < frames; ++i) {
        const i32 x = (mix[2 * i] >> 1) + (mix[2 * i + 1] >> 1);
        const i32 d = static_cast<i32>(read[i]) << 9;
        low += (d - low) >> 1;                                   // a little duller each cycle
        const opxy::i16 back = static_cast<opxy::i16>(clamp((x + multiply(low, feedback)) >> 9, -32767, 32767));
        write[i] = back;
        again[i] = back;
        const i32 y = multiply(d, wet);
        mix[2 * i] += y;
        mix[2 * i + 1] += y;
    }
    v.comb_at += frames;
    v.comb_low = low;
}

__attribute__((noinline)) static void crush_block(i32* mix, i32 drive, i32 mask, u32 every, i32* held, u32& count) {
    if (every == 1 && count == 0 && mask == -1) {
        // The first 60%: the drive and the clip alone, every sample.
        for (u32 i = 0; i < 2 * frames; ++i) {
            // drive is an eighth of the gain (Q31), so up to 8 times into the clip
            const i32 x = multiply(clamp(mix[i], -8 * one + 1, 8 * one - 1), drive) << 3;
            mix[i] = clamp(x, -one + (one >> 3), one - (one >> 3));
        }
        held[0] = mix[2 * frames - 2];
        held[1] = mix[2 * frames - 1];
        return;
    }
    u32 c = count;
    for (u32 i = 0; i < frames; ++i) {
        if (c == 0) {
            for (u32 side = 0; side < 2; ++side) {
                const i32 x = multiply(clamp(mix[2 * i + side], -8 * one + 1, 8 * one - 1), drive) << 3;
                held[side] = clamp(x, -one + (one >> 3), one - (one >> 3)) & mask;
            }
            c = every;
        }
        --c;
        mix[2 * i] = held[0];
        mix[2 * i + 1] = held[1];
    }
    count = c;
}

// The block out, both sides: Q24 to Q31 at half scale, the declared peak; and with the space added.
__attribute__((noinline)) static void out_of(i32* out, const i32* mix) {
    for (u32 i = 0; i < 2 * frames; ++i) out[i] = clamp(mix[i], -one, one) << 6;
}
__attribute__((noinline)) static void out_with(i32* out, const i32* mix, const i32* wet) {
    for (u32 i = 0; i < 2 * frames; ++i) out[i] = clamp(mix[i] + wet[i], -one, one) << 6;
}
// crush's drive and clip (as crush_block's first 60%) and the way out, in one pass; and with the space added.
static inline i32 driven(i32 x, i32 drive) {
    return clamp(multiply(clamp(x, -8 * one + 1, 8 * one - 1), drive) << 3, -one + (one >> 3), one - (one >> 3));
}
__attribute__((noinline)) static void crush_out(i32* out, const i32* mix, i32 drive, i32* held) {
    for (u32 i = 0; i < 2 * frames; ++i) out[i] = driven(mix[i], drive) << 6;   // the clip is inside the declared peak
    held[0] = driven(mix[2 * frames - 2], drive);
    held[1] = driven(mix[2 * frames - 1], drive);
}
__attribute__((noinline)) static void crush_out_with(i32* out, const i32* mix, const i32* wet, i32 drive, i32* held) {
    for (u32 i = 0; i < 2 * frames; ++i) out[i] = clamp(driven(mix[i], drive) + wet[i], -one, one) << 6;
    held[0] = driven(mix[2 * frames - 2], drive);
    held[1] = driven(mix[2 * frames - 1], drive);
}
// The snap, click and blip lanes (aux) into the mix, and as far as ring sends them, to the space.
__attribute__((noinline)) static void join(i32* mix, const i32* aux, i32* bus, i32 send) {
    if (!send) {
        for (u32 i = 0; i < 2 * frames; ++i) mix[i] += aux[i];
        return;
    }
    for (u32 i = 0; i < frames; ++i) {
        const i32 l = aux[2 * i], r = aux[2 * i + 1];
        mix[2 * i] += l;
        mix[2 * i + 1] += r;
        bus[i] += multiply((l >> 1) + (r >> 1), send);
    }
}
}  // namespace engine

// ------------------------------------------------------------------ the callbacks
void initialize(Voice& voice, const opxy::SampleView& sample) {
    voice.sample = sample;
    voice.random = 0x9e3779b9u;
    voice.noise = 0x2545f491u;
    voice.cycle = engine::clock.cycle;
}

void start(Voice& voice) {
    // A fresh note places itself on its first two blocks (render): its rank,
    // its seed, and whether it restarts the clock. It never moves the census
    // itself, so the shared clock keeps time for the voices already playing.
    voice.stage = 0;
    voice.cycle = engine::clock.cycle - 1;
    for (u32 k = 0; k < lane_count; ++k) {
        voice.lane[k].amp = 0;
        voice.lane[k].repeats = 0;
        voice.rot[k] = voice.extra[k] = voice.potential[k] = 0;
    }
    voice.spikes = 0;
    voice.jump = voice.jump_left = 0;
    voice.crushed[0] = voice.crushed[1] = 0;
    voice.crush_count = 0;
    voice.rest = voice.bend = voice.bend_ms = 0;
    voice.drop = 0;
    for (u32 i = 0; i < sizeof voice.comb / sizeof voice.comb[0]; ++i) voice.comb[i] = 0;
    voice.comb_at = 0;
    voice.comb_low = 0;
    voice.comb_cycle = 0;
}

void render_sample(Voice& voice, const opxy::SampleBlock& block) {
    using namespace engine;
    const u32 seen = clock.cycle;   // the census before this voice counts itself
    bool late = false;
    const bool leads = census(voice, block.transport);
    i32 mix[2 * frames], aux[2 * frames], wet[2 * frames];
    bool sent = false;   // aux holds this block's snap, click and blip (each stretch is written or cleared as it comes: run)
    // The voice that moved the census runs the space on the last cycle's sends, unless
    // that block was worked out early (below). A fresh voice never moves the census,
    // so the space is never skipped by a voice's silent first block.
    bool heard = false;
    const i32* wetter = wet;
    if (leads) {
        if (space.early_ready) {
            space.early_ready = 0;
            heard = space.early_heard != 0;
            wetter = space.early;
        } else {
            heard = space_run(wet);
        }
    }

    const i32* p = block.parameters;
    // The keys held: every Brain Dance voice that rendered in the last whole block.
    const i32 keys = static_cast<i32>(clock.last < 1 ? 1 : clock.last > 8 ? 8 : clock.last);
    const i32 more = (keys - 1) * 4681;                          // Q15: 0 with one key, 1 with eight

    if (voice.stage < 2) {
        if (voice.stage == 0) {
            // Its first block: silent, noting the census cycle. If another
            // voice moves the census on before this one renders again,
            // others are sounding.
            voice.stage = 1;
            voice.placed = clock.cycle;
            for (u32 i = 0; i < (block.output_channels == 2 ? 2 * frames : frames); ++i) block.output[i] = 0;
            return;
        }
        // Its second block: alone, with the sequencer stopped, it restarts
        // the clock on its key; with others it joins their clock, and takes
        // the next role.
        const bool others = seen != voice.placed;
        const u32 before = others ? (clock.last > 1 ? clock.last - 1 : 1) : 0;
        voice.rank = before > 3 ? 3 : before;
        voice.seed = scramble(block.pitch[0] ^ (voice.rank * 0x632be5abu) ^ 0x5bd1e995u) | 1u;
        voice.random = voice.seed;
        voice.bits = scramble(voice.seed) | 1u;
        for (u32 k = 0; k < lane_count; ++k) voice.rot[k] = static_cast<i32>(voice.seed >> (8 * k)) & 7;
        if (voice.rank == 0) voice.rot[kLow] = 0;   // the first key keeps its low lane on the one
        voice.stage = 2;
        if (!others && !block.transport.playing) {
            clock.tick = 0;
            clock.frac = 0;
            space.gate = 0;
        } else if (clock.inc && clock.frac / clock.inc <= 2 * frames + 8) {
            late = true;   // a tick fell in its two placing blocks: play it now
        }
    }

    Shape s;
    // the first encoder's sixty steps: an algorithm, and one of its ten variants
    u32 step = (static_cast<u32>(q15(p[kAlgo])) * (algorithm_count * variant_count)) >> 15;
    if (step >= algorithm_count * variant_count) step = algorithm_count * variant_count - 1;
    s.algo = (step * 6554u) >> 16;                 // step / 10, without the division
    s.variant = step - s.algo * variant_count;
    s.key = block.pitch[0];
    if (!clock.inc) {
        s.s32 = 2756;
    } else {
        if (clock.s32_inc != clock.inc) { clock.s32_inc = clock.inc; clock.s32 = 0xffffffffu / clock.inc; }
        s.s32 = clock.s32;
    }
    s.dense = clamp(q15(p[kDense]) + (more >> 2), 0, 32767);
    s.chaos = clamp(q15(p[kChaos]) + (more * 3 >> 3), 0, 32767);
    s.glitch = clamp(q15(p[kGlitch]) + (more * 3 >> 3), 0, 32767);
    s.tone = q15(p[kTone]);
    s.color = q15(p[kColor]);
    s.ring = q15(p[kRing]);
    s.crush = q15(p[kCrush]);
    // the sample's snippets, once the analysis is ready and inside a non-empty trim
    s.count = 0;
    s.rec = 0;
    const opxy::SampleAnalysis* a = block.analysis;
    if (a && a->words && a->word_count >= harvest::result_words && a->words[0] == harvest::magic && a->sample.data &&
        a->sample.frames && (a->sample.channels == 1 || a->sample.channels == 2) && block.region.end > block.region.begin) {
        s.rec = a->words + harvest::header_words;
        s.count = harvest::count_of(a->words);
        if (s.count > (harvest::result_words - harvest::header_words) / 4) s.count = 0;
        s.pcm = a->sample.data;
        s.channels = a->sample.channels;
        s.hz = a->sample.rate ? a->sample.rate : 44100;
        s.rate = (s.hz * 1522u) >> 10;   // 65536 / 44100 = 1.48608
        s.begin = block.region.begin;
        s.end = block.region.end < a->sample.frames ? block.region.end : a->sample.frames;
        const u32 level = block.region.level >> 12;   // Q27 to Q15
        s.level = level > 131071 ? 131071 : level;
    }

    // The block in stretches between events: the clock's ticks and the lanes' ratchets.
    u32 tick = clock.tick;
    const u32 inc = clock.inc ? clock.inc : increment_of(1200);
    // samples to the next tick: at 0 if the block starts on one. A block on from the
    // last answer with no tick between, it is that less a block; the same block
    // (another voice's), it is that; only otherwise is it divided out.
    u32 to_tick = 0;
    if (clock.frac != 0) {
        if (clock.until_inc == inc && clock.until_frac == clock.frac) {
            to_tick = clock.until;
        } else if (clock.until_inc == inc && clock.until > frames && clock.until_frac + inc * frames == clock.frac) {
            to_tick = clock.until - frames;
        } else {
            to_tick = (0u - clock.frac) / inc + (((0u - clock.frac) % inc) ? 1 : 0);
        }
        clock.until_inc = inc;
        clock.until_frac = clock.frac;
        clock.until = to_tick;
    }
    if (clock.frac != 0) ++tick;
    u32 at = 0;
    if (late && clock.frac != 0) engine::tick(voice, clock.tick, s);
    // A quiet block (nothing sounding, no ratchet to come) with a tick close ahead has time to spare,
    // and the tick's own block has none. Three blocks before the tick, rehearse it; two before, read
    // through the sample its hits would play (both above). One block before, a voice playing alone
    // works out the space's next block now, so the tick's block needn't: alone, nothing else can send
    // to the space this cycle, so that block comes out the same.
    bool quiet = voice.stage == 2 && !late;
    for (u32 k = 0; k < lane_count; ++k)
        if (voice.lane[k].amp > 0 || voice.lane[k].repeats > 0) quiet = false;
    if (quiet && to_tick >= 3 * frames && to_tick < 4 * frames) rehearse(voice, tick, s);
    if (quiet && to_tick >= 2 * frames && to_tick < 3 * frames && rehearsal.of == &voice && rehearsal.tick == tick) warm(rehearsal.copy);
    const bool early = quiet && leads && clock.last == 1 && to_tick >= frames && to_tick < 2 * frames;
    for (;;) {
        if (to_tick == at && at < frames) {
            engine::tick(voice, tick, s);
            ++tick;
            to_tick = at + s.s32;   // beyond this block: a 32nd is at least 551 samples
        }
        if (at >= frames) break;
        u32 until = frames;
        if (to_tick < until) until = to_tick;
        for (u32 k = 0; k < lane_count; ++k) {
            const Lane& l = voice.lane[k];
            if (l.repeats > 0 && at + static_cast<u32>(l.wait) < until) until = at + static_cast<u32>(l.wait);
        }
        const u32 n = until - at;
        if (n) {
            if (!sent && (voice.lane[kSnap].amp > 0 || voice.lane[kClick].amp > 0 || voice.lane[kBlip].amp > 0)) {
                for (u32 i = 0; i < 2 * at; ++i) aux[i] = 0;   // the stretches before had none of them
                sent = true;
            }
            run(voice, mix + 2 * at, aux + 2 * at, n, sent);
        }
        for (u32 k = 0; k < lane_count; ++k) {
            Lane& l = voice.lane[k];
            if (l.repeats <= 0) continue;
            l.wait -= static_cast<i32>(n);
            if (l.wait <= 0) repeat(voice, k, s);
        }
        at = until;
    }

    // the snap, click and blip lanes join the mix, and feed the space as far as ring sends them
    if (sent) join(mix, aux, space.bus, (s.ring << 16) >> (voice.rank > 2 ? 2 : voice.rank));   // later keys send less, so chords don't flood it
    // ring's comb, and crush
    if (s.ring > 16384) {
        if (voice.comb_key != s.key || !voice.comb_cycle) {              // worked out once a key, not once a block
            voice.comb_key = s.key;
            voice.comb_cycle = 0xffffffffu / fold(s.key, 500 * hz);      // samples a cycle at 500 Hz to 1 kHz: 44 to 88
        }
        const u32 cycle = voice.comb_cycle;
        const i32 amount = (s.ring - 16384) * 2;                          // Q15, over ring's upper half
        comb_block(mix, voice, cycle < 16 ? 16 : cycle > 120 ? 120 : cycle, (amount * 31785) << 1, amount << 15);
    }
    bool out = false;   // the block is on its way out
    if (s.crush) {
        const i32 c = s.crush;
        const i32 d = c < 19661 ? c : 19661;                              // the first 60%: drive alone, x1 to x8
        const i32 drive = 268435456 + d * 95563;
        const i32 rest = c - d;                                           // above it: 0..13106
        const u32 every = 1 + static_cast<u32>(((rest >> 5) * (rest >> 5) * 23) >> 17);   // 1..23 samples a value
        const u32 bits = static_cast<u32>(rest * 30 >> 15);              // up to 11 of 24 bits gone
        if (every == 1 && bits == 0 && voice.crush_count == 0 && !voice.drop && block.output_channels == 2) {
            // the usual block (the first 60%, both sides out, no dropout): the drive, the clip and the way out in one pass
            if (heard) crush_out_with(block.output, mix, wetter, drive, voice.crushed);
            else crush_out(block.output, mix, drive, voice.crushed);
            out = true;
        } else {
            crush_block(mix, drive, ~((1 << bits) - 1), every, voice.crushed, voice.crush_count);
        }
    }
    if (!out) {
        // glitch's dropout: dead silence from the block's start
        if (voice.drop) {
            const u32 n = voice.drop < frames ? voice.drop : frames;
            for (u32 i = 0; i < 2 * n; ++i) mix[i] = 0;
            voice.drop -= n;
        }
        // Out, with the space from the voice that leads the cycle: Q24 to Q31 at half scale, the declared peak.
        if (block.output_channels == 2) {
            if (heard) out_with(block.output, mix, wetter);
            else out_of(block.output, mix);
        } else {
            if (heard) for (u32 i = 0; i < 2 * frames; ++i) mix[i] += wetter[i];
            for (u32 i = 0; i < frames; ++i) block.output[i] = clamp((mix[2 * i] >> 1) + (mix[2 * i + 1] >> 1), -one, one) << 6;
        }
    }
    // the space's next block, early (see quiet, above)
    if (early) {
        space.early_heard = space_run(space.early) ? 1 : 0;
        space.early_ready = 1;
    }
}
}
