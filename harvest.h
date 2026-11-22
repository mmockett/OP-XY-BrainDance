// harvest.h: Brain Dance's sample analysis (README, "Samples"). When a sample
// is loaded, the SDK runs this on the OP-XY's file worker, a bounded step at a
// time (sampleAnalysis), and hands every voice the result: the sample's most
// usable snippets, the best few of each kind.
//
//   low hit     a hit dominated by low energy, that decays: a kick
//   snap hit    a noisy, bright hit with some body: a snare, a clap
//   click hit   a very short, bright hit: a tick, a hat
//   blip hit    a short pitched hit: a blip, a pluck
//   tone loop   a steady pitched stretch, cut to whole cycles: an oscillator
//   noise loop  a steady noisy stretch: a noise source
//
// It finds moments where one character dominates; it can't pull a kick out of
// a full mix. Integer arithmetic only, no allocation.
//
// The steps: (0) one streaming pass measures every 256-frame hop: its level,
// the share of its energy below about 200 Hz and in the highs, and its zero
// crossings, one word a hop in the storage; (1) the hops are searched for
// hits (a sharp rise, then a fall) and each is scored for the kinds it could
// be; (2) and for steady stretches; (3) each pitched candidate's period is
// found by average magnitude difference, one candidate a step; (4) the best
// are written to the start of the storage.
#pragma once
#include <opxy/sample_analysis.hpp>

namespace braindance {
namespace harvest {
using opxy::i16;
using opxy::i32;
using opxy::u16;
using opxy::u32;

const u32 hop = 256;                 // frames a feature word measures
const u32 max_frames = 0x000fffffu;  // the longest sample the SDK loads
const u32 result_words = 128;        // the result, at the start of the storage
const u32 magic = 0x42443031u;       // "BD01"
const u32 keep = 4;                  // the best kept of each kind
const u32 longest = 32767;           // frames a snippet may last (positions are Q16)
const u32 candidates = 24;           // pitch checks at most
const u32 window = 1024;             // frames a pitch check compares
const u32 max_lag = 1102;            // its longest period: 40 Hz at 44.1 kHz

enum Kind { kLowHit = 0, kSnapHit, kClickHit, kBlipHit, kToneLoop, kNoiseLoop, kinds };

// A snippet: where it starts and how long it is (frames), its gain to a common
// level (Q12), its score, and for a pitched one its period (Q8 frames) and,
// for a tone loop, how many whole cycles it holds (as a power of two).
struct Snippet { u32 start, length, gain, score, period, cycles; };

// The result's words: magic, snippet count, the sample's rate, 0; then four
// words a snippet, sorted by kind, best first:
//   start | length + kind << 16 + cycles << 20 | gain + score << 16 | period (Q8)
const u32 header_words = 4;
inline u32 count_of(const u32* words) { return words[1]; }
inline u32 kind_of(const u32* r) { return (r[1] >> 16) & 15u; }
inline u32 start_of(const u32* r) { return r[0]; }
inline u32 length_of(const u32* r) { return r[1] & 0xffffu; }
inline u32 cycles_of(const u32* r) { return (r[1] >> 20) & 7u; }
inline u32 gain_of(const u32* r) { return r[2] & 0xffffu; }
inline u32 score_of(const u32* r) { return r[2] >> 16; }
inline u32 period_of(const u32* r) { return r[3]; }

// A feature word: level (8 bits, about 3/8 dB a step, 0 silent) | low share | high share | zero crossings.
inline u32 level_at(const u32* f, u32 i) { return f[i] & 255u; }
inline u32 low_at(const u32* f, u32 i) { return (f[i] >> 8) & 255u; }
inline u32 high_at(const u32* f, u32 i) { return (f[i] >> 16) & 255u; }
inline u32 zcr_at(const u32* f, u32 i) { return f[i] >> 24; }

struct Candidate { u32 start, frames, kind, score, zcr, loud; };

struct Preparation {
    opxy::SampleView sample;
    u32* storage;
    u32 words, hops, frames, stage;
    // the streaming pass
    u32 frame, n, z, e, el, eh, top;
    i32 lp, prev, sign, a;
    // the searches
    u32 at, last_onset;
    Candidate cand[candidates];
    u32 cands, checked;
    u16 diff[max_lag + 2];
    Snippet best[kinds][keep];
    u32 kept[kinds];
};

inline i32 mono(const opxy::SampleView& s, u32 i) {
    if (i >= s.frames) return 0;
    if (s.channels == 2) return (static_cast<i32>(s.data[2 * i]) + s.data[2 * i + 1]) >> 1;
    return s.data[i * s.channels];
}
inline u32 magnitude(i32 x) { return static_cast<u32>(x < 0 ? -x : x); }
inline u32 square(i32 x) { return static_cast<u32>(x * x); }
// About 8 log2(e): 3/8 dB a step; 0 only for silence.
inline u32 level_of(u32 e) {
    if (!e) return 0;
    u32 b = 0;
    while ((e >> b) > 1u) ++b;
    const u32 frac = b >= 3 ? (e >> (b - 3)) & 7u : (e << (3 - b)) & 7u;
    const u32 v = b * 8 + frac + 1;
    return v > 255 ? 255 : v;
}
inline u32 share(u32 part, u32 whole, u32 scale) {
    const u32 s = (part >> 8) * scale / ((whole >> 8) + 1);
    return s > 255 ? 255 : s;
}

// Keeps a snippet among its kind's best, unless a better one already starts within 2 hops.
inline void offer(Preparation& job, u32 kind, const Snippet& s) {
    Snippet* list = job.best[kind];
    u32& n = job.kept[kind];
    for (u32 i = 0; i < n; ++i) {
        const u32 gap = list[i].start > s.start ? list[i].start - s.start : s.start - list[i].start;
        if (gap < 2 * hop) {
            if (list[i].score >= s.score) return;
            for (u32 j = i; j + 1 < n; ++j) list[j] = list[j + 1];   // drop the worse twin
            --n;
            break;
        }
    }
    u32 at = n;
    while (at > 0 && list[at - 1].score < s.score) --at;
    if (at >= keep) return;
    const u32 last = n < keep ? n : keep - 1;
    for (u32 j = last; j > at; --j) list[j] = list[j - 1];
    list[at] = s;
    if (n < keep) ++n;
}

// The gain that brings a peak (or for a loop, a mean magnitude) to the target, Q12, 1/4 to 16.
inline u32 gain_for(u32 measured, u32 target) {
    if (!measured) return 4096;
    u32 g = target * 64u / measured * 64u;
    if (g < 1024) g = 1024;
    if (g > 65535) g = 65535;
    return g;
}
inline u32 peak_of(const opxy::SampleView& s, u32 start, u32 length) {
    u32 p = 0;
    for (u32 i = start; i < start + length; ++i) { const u32 m = magnitude(mono(s, i)); if (m > p) p = m; }
    return p;
}
inline u32 mean_of(const opxy::SampleView& s, u32 start, u32 length) {
    u32 sum = 0;
    for (u32 i = start; i < start + length; ++i) sum += magnitude(mono(s, i)) >> 2;
    return length ? (sum / length) << 2 : 0;
}
inline u32 rising_zero(const opxy::SampleView& s, u32 from, u32 within) {
    for (u32 i = from + 1; i < from + within && i < s.frames; ++i)
        if (mono(s, i - 1) < 0 && mono(s, i) >= 0) return i;
    return from;
}

// ---- step 0: the features, frames at a time
inline void measure(Preparation& job, u32 frames) {
    u32* f = job.storage + result_words;
    const u32 last = job.frames - job.frame < frames ? job.frames : job.frame + frames;
    for (; job.frame < last; ++job.frame) {
        const i32 m = mono(job.sample, job.frame);
        job.lp += ((m - job.lp) * job.a) >> 15;
        const i32 d = m - job.prev;
        job.prev = m;
        job.e += square(m >> 5);
        job.el += square(job.lp >> 5);
        job.eh += square(d >> 6);
        if (job.sign >= 0 && m < -64) { job.sign = -1; ++job.z; }
        else if (job.sign <= 0 && m > 64) { job.sign = 1; ++job.z; }
        if (++job.n == hop || job.frame + 1 == job.frames) {
            const u32 level = level_of(job.e);
            const u32 word = level | share(job.el, job.e, 255) << 8 | share(job.eh, job.e, 510) << 16 |
                             (job.z > 255 ? 255u : job.z) << 24;
            const u32 h = job.frame / hop;
            if (h < job.hops) f[h] = word;
            if (level > job.top) job.top = level;
            job.n = job.z = job.e = job.el = job.eh = 0;
        }
    }
}

// ---- step 1: the hits, a few hundred hops a step
inline bool onset(const u32* f, u32 i, u32 floor) {
    if (i == 0) return false;
    const u32 l = level_at(f, i), p = level_at(f, i - 1), p2 = i >= 2 ? level_at(f, i - 2) : 0;
    return l >= floor + 16 && l > p && (l >= p + 12 || l >= p2 + 20);
}
inline void hits(Preparation& job) {
    const u32* f = job.storage + result_words;
    const u32 floor = job.top > 96 ? job.top - 96 : 0;
    const u32 stop = job.at + 256 < job.hops ? job.at + 256 : job.hops;
    for (; job.at < stop; ++job.at) {
        const u32 i = job.at;
        if (i < job.last_onset + 3 || !onset(f, i, floor)) continue;
        job.last_onset = i;
        u32 p = i;
        for (u32 k = i + 1; k < i + 4 && k < job.hops; ++k) if (level_at(f, k) > level_at(f, p)) p = k;
        const u32 peak = level_at(f, p);
        u32 j = p + 1;
        while (j < job.hops && j - i < 127 && level_at(f, j) + 64 > peak && !onset(f, j, floor)) ++j;
        const u32 span = j - i, tail = j - p;
        // a hit falls away (or is cut by the next): a sound that holds is a steady stretch, not a hit
        const bool falls = j < job.hops && (level_at(f, j) + 64 <= peak || onset(f, j, floor));
        // the attack's character: the hops up to just past the peak
        u32 low = 0, high = 0, zcr = 0, n = 0;
        for (u32 k = i; k <= p + 1 && k < j; ++k, ++n) { low += low_at(f, k); high += high_at(f, k); zcr += zcr_at(f, k); }
        low /= n; high /= n; zcr /= n;
        const u32 loud = peak - floor;
        // where it really starts: the first frame within an eighth of its peak, less a little
        const u32 from = i > 0 ? (i - 1) * hop : 0, to = (p + 1) * hop;
        const u32 pk = peak_of(job.sample, from, to < job.frames ? to - from : job.frames - from);
        u32 start = from;
        while (start < to && start < job.frames && magnitude(mono(job.sample, start)) * 8 < pk) ++start;
        start = start > from + 32 ? start - 32 : from;
        u32 length = j * hop > start ? j * hop - start : hop;
        if (length > longest) length = longest;
        if (start + length > job.frames) length = job.frames - start;
        if (length < 64) continue;
        Snippet s = {start, length, gain_for(peak_of(job.sample, start, length), 29491), 0, 0, 0};
        // its score as each kind; it's kept as the one it suits best
        u32 kind = kinds, score = 0;
        if (falls && low >= 110 && zcr <= 40 && tail >= 3 && tail <= 100) {
            const u32 sc = low * 2 + (255 - high) + (tail < 40 ? tail : 40) * 2 + loud;
            if (sc > score) { score = sc; kind = kLowHit; }
        }
        if (falls && zcr >= 30 && high >= 70 && tail >= 2 && tail <= 60) {
            const u32 sc = zcr + high + (low < 120 ? low : 120) / 2 + loud;
            if (sc > score) { score = sc; kind = kSnapHit; }
        }
        if (span <= 4 && high >= 50) {
            const u32 sc = high + (5 - span) * 60 + loud;
            if (sc > score) { score = sc; kind = kClickHit; }
        }
        if (kind < kinds) { s.score = score; offer(job, kind, s); }
        // a short hit not too noisy may be pitched: a blip, if the pitch check agrees
        if (falls && zcr >= 4 && zcr < 70 && low < 200 && tail >= 2 && tail <= 60 && job.cands < candidates) {
            const Candidate c = {start, length, kBlipHit, loud + 100, zcr, loud};
            job.cand[job.cands++] = c;
        }
    }
}

// ---- step 2: steady stretches, all at once (one pass over the feature words)
inline void steady(Preparation& job) {
    const u32* f = job.storage + result_words;
    const u32 floor = job.top > 96 ? job.top - 96 : 0;
    u32 begin = 0, run = 0, zsum = 0, lsum = 0;
    for (u32 k = 1; k <= job.hops; ++k) {
        const bool holds = k < job.hops && level_at(f, k) >= floor + 24 && !onset(f, k, floor) &&
                           level_at(f, k) + 8 >= level_at(f, k - 1) && level_at(f, k) <= level_at(f, k - 1) + 8;
        if (holds) {
            if (!run) { begin = k; zsum = lsum = 0; }
            ++run;
            zsum += zcr_at(f, k);
            lsum += level_at(f, k);
            continue;
        }
        if (run >= 4) {
            const u32 start = (begin + 1) * hop;
            u32 frames = (run - 1) * hop;
            if (frames > 16384) frames = 16384;
            if (start + frames <= job.frames) {
                const u32 loud = lsum / run - floor;
                const Candidate c = {start, frames, kToneLoop, loud + (run < 40 ? run : 40), zsum / run, loud};
                if (job.cands < candidates) job.cand[job.cands++] = c;
                else {   // full: replace the weakest steady stretch if this is better
                    u32 worst = candidates;
                    for (u32 i = 0; i < candidates; ++i)
                        if (job.cand[i].kind == kToneLoop && (worst == candidates || job.cand[i].score < job.cand[worst].score)) worst = i;
                    if (worst < candidates && job.cand[worst].score < c.score) job.cand[worst] = c;
                }
            }
        }
        run = 0;
    }
}

// ---- step 3: one candidate's period, by average magnitude difference
inline void check(Preparation& job, const Candidate& c) {
    const opxy::SampleView& s = job.sample;
    const u32 rate = s.rate ? s.rate : 44100;
    const u32 min_lag = rate / 2000 > 8 ? rate / 2000 : 8;
    u32 top_lag = rate / 40 < max_lag ? rate / 40 : max_lag;
    if (c.frames < window + min_lag + 2) return;
    if (c.frames - window - 2 < top_lag) top_lag = c.frames - window - 2;
    if (top_lag <= min_lag + 2) return;
    u32 sum = 1;
    for (u32 i = 0; i < window; i += 2) sum += magnitude(mono(s, c.start + i));
    u32 mean = 0;
    for (u32 lag = min_lag; lag <= top_lag; ++lag) {
        u32 d = 0;
        for (u32 i = 0; i < window; i += 2) d += magnitude(mono(s, c.start + i) - mono(s, c.start + i + lag));
        const u32 r = d / (sum >> 6 ? sum >> 6 : 1);   // Q6 of the mean magnitude: noise near 64..90, a tone near 0
        job.diff[lag] = static_cast<u16>(r > 0xffff ? 0xffff : r);
        mean += job.diff[lag];
    }
    mean /= top_lag - min_lag + 1;
    // leave the valley round lag 0 (a low sound barely changes in a few frames), then the best lag
    u32 from = min_lag;
    while (from <= top_lag && job.diff[from] * 2 < mean) ++from;
    u32 best = 0xffff, at = 0;
    for (u32 lag = from + 1; lag < top_lag; ++lag)
        if (job.diff[lag] < best && job.diff[lag] <= job.diff[lag - 1] && job.diff[lag] <= job.diff[lag + 1]) { best = job.diff[lag]; at = lag; }
    if (!at) return;
    // the shortest lag nearly as good as the best, and a minimum there: no octave errors
    const u32 near = best + best / 4 + 2;
    for (u32 lag = from + 1; lag < at; ++lag)
        if (job.diff[lag] <= near && job.diff[lag] <= job.diff[lag - 1] && job.diff[lag] <= job.diff[lag + 1]) { at = lag; break; }
    const u32 rough = best > 64 ? 0 : 64 - best;   // how pitched: 64 a pure tone
    if (c.kind == kBlipHit) {
        if (best > 16) return;
        const Snippet sn = {c.start, c.frames, gain_for(peak_of(s, c.start, c.frames), 29491), c.score + rough * 4, at << 8, 0};
        offer(job, kBlipHit, sn);
        return;
    }
    if (best <= 16) {   // a tone: a loop of whole cycles, at least 256 frames, from a rising zero
        u32 cycles = 0;
        while ((at << cycles) < 256 && cycles < 3) ++cycles;
        const u32 length = at << cycles;
        const u32 start = rising_zero(s, c.start, at);
        if (start + length > c.start + c.frames || length > longest) return;
        const Snippet sn = {start, length, gain_for(mean_of(s, start, length), 6553), rough * 8 + c.loud + c.score, at << 8, cycles};
        offer(job, kToneLoop, sn);
    } else if (best >= 40 && c.zcr >= 30) {   // noise: as much as 8192 frames of it
        const u32 start = rising_zero(s, c.start, 256);
        u32 length = c.start + c.frames - start;
        if (length > 8192) length = 8192;
        if (length < 1024) return;
        const Snippet sn = {start, length, gain_for(mean_of(s, start, length), 6553), c.zcr + c.loud + length / 256, 0, 0};
        offer(job, kNoiseLoop, sn);
    }
}

// ---- step 4: the result
inline void publish(Preparation& job) {
    u32* w = job.storage;
    for (u32 i = 0; i < result_words; ++i) w[i] = 0;
    w[0] = magic;
    w[2] = job.sample.rate;
    u32 n = 0;
    for (u32 k = 0; k < kinds; ++k)
        for (u32 i = 0; i < job.kept[k]; ++i) {
            const Snippet& s = job.best[k][i];
            u32* r = w + header_words + 4 * n;
            r[0] = s.start;
            r[1] = (s.length & 0xffffu) | k << 16 | (s.cycles & 7u) << 20;
            r[2] = (s.gain & 0xffffu) | (s.score > 0xffff ? 0xffffu : s.score) << 16;
            r[3] = s.period;
            ++n;
        }
    w[1] = n;
}
}  // namespace harvest

// ------------------------------------------------------------------ the SDK's callbacks (sampleAnalysis)
using harvest::Preparation;

inline opxy::u32 analysis_words(const opxy::SampleView& sample) {
    if (!sample.data || !sample.frames || sample.frames > harvest::max_frames || (sample.channels != 1 && sample.channels != 2)) return 0;
    return harvest::result_words + (sample.frames + harvest::hop - 1) / harvest::hop;
}

inline bool prepare_begin(Preparation& job, const opxy::SampleView& sample, opxy::u32* storage, opxy::u32 words) {
    if (!storage || words < analysis_words(sample) || !analysis_words(sample)) return false;
    job.sample = sample;
    job.storage = storage;
    job.words = words;
    job.frames = sample.frames;
    job.hops = (sample.frames + harvest::hop - 1) / harvest::hop;
    job.stage = 0;
    job.frame = job.n = job.z = job.e = job.el = job.eh = job.top = 0;
    job.lp = job.prev = job.sign = 0;
    // the low share's one-pole low-pass at about 200 Hz: 2 pi 200 / rate, Q15
    job.a = static_cast<opxy::i32>(41176000u / (sample.rate ? sample.rate : 44100));
    job.at = job.last_onset = job.cands = job.checked = 0;
    for (opxy::u32 k = 0; k < harvest::kinds; ++k) job.kept[k] = 0;
    for (opxy::u32 i = 0; i < words; ++i) storage[i] = 0;
    return true;
}

inline opxy::PrepareStatus prepare_step(Preparation& job, opxy::u32 frames, opxy::u32& cursor) {
    using namespace harvest;
    switch (job.stage) {
    case 0:
        measure(job, frames ? frames : 1);
        cursor = job.frame;
        if (job.frame >= job.frames) { job.stage = 1; job.last_onset = 0; }
        return opxy::prepare_running;
    case 1:
        hits(job);
        if (job.at >= job.hops) job.stage = 2;
        break;
    case 2:
        steady(job);
        job.stage = 3;
        break;
    case 3:
        if (job.checked < job.cands) check(job, job.cand[job.checked++]);
        if (job.checked >= job.cands) job.stage = 4;
        break;
    default:
        publish(job);
        cursor = job.frames;
        return opxy::prepare_done;
    }
    cursor = job.frames > 0 ? job.frames - 1 : 0;   // the searches: nearly there
    return opxy::prepare_running;
}

inline bool prepared(const Preparation& job, opxy::SampleAnalysis& out) {
    if (job.stage < 4 || job.storage[0] != harvest::magic) return false;
    out.sample = job.sample;
    out.words = job.storage;
    out.word_count = job.words;
    out.info = job.storage[1];
    return true;
}
}  // namespace braindance
