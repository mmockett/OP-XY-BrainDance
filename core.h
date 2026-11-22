#pragma once
#include <opxy/sample_engine.hpp>

namespace braindance {
using opxy::i32;
using opxy::u32;

// Brain Dance: a procedural glitch-beat engine. It is an OP-XY sample engine
// (base "sampler") so that it receives the project's tempo every block
// (SampleBlock.transport), as Recur and Ghost do. With no sample it plays its
// own synthesis; with one, each lane plays the best snippet the analysis
// salvaged for it (harvest.h), or keeps its synthesis where none was. Each
// held key is one voice. A voice plays four percussion lanes (low, snap,
// click, blip), all tuned to the key, from one of six pattern algorithms on a
// clock every voice shares. The snap, click and blip lanes feed one gated,
// staggered reverb every voice shares; ring adds it and a comb at the key,
// glitch adds dropouts and stutters, crush drives everything into a clip. The more keys are held, the denser, the more
// chaotic and the glitchier every voice becomes, and each newer key takes a
// higher, busier role in the polymeter.

const u32 lane_count = 4;
enum LaneKind { kLow = 0, kSnap = 1, kClick = 2, kBlip = 3 };
enum Control { kAlgo = 0, kDense, kChaos, kGlitch, kTone, kColor, kRing, kCrush };
enum Algorithm { kLanes = 0, kBreak, kData, kEuclid, kPulse, kShred, algorithm_count };
// Each algorithm has ten variants, 0 to 9, on the same encoder: sixty steps.
// A variant changes what no control reaches: the algorithm's cycle lengths
// and patterns, which lanes lead, how ratchets divide and the lanes' registers.
const u32 variant_count = 10;

// One lane: a percussion voice inside the note's voice.
struct Lane {
    u32 phase, inc;          // the main oscillator (low: its resting pitch; click: its tone)
    u32 phase2, inc2;        // the second (snap: the body; click: the bits' clock; blip: the modulator)
    i32 sweep, sweep_dec;    // low: the pitch drop's excess increment and its fall a sample (Q31)
    i32 amp, dec;            // level (Q31) and its fall a sample (Q31)
    i32 amp2, dec2;          // snap: the body's level; blip: the FM index
    i32 cut;                 // samples left before a hard gate shuts it (0: none)
    i32 f, damp, s1, s2;     // snap: the noise band-pass (Chamberlin), Q31 coefficients, Q24 state
    i32 held;                // click: the bits' held value (Q24)
    i32 w[3];                // click: sine, square and bits weights (Q31); snap: w[0] the highs let past its band
    i32 left, right;         // pan gains (Q31)
    i32 repeats, every, wait;   // a ratchet: hits to come, samples between them, samples to the next
    i32 again;               // its next hit's level (Q31)
    i32 rise;                // 1: each repeat climbs in pitch (a roll)
    i32 climb;               // repeats so far
    // a snippet of the sample in place of the synthesis (src 0: none)
    u32 src;                 // 1 played once, 2 a tone loop, 3 a noise loop
    const opxy::i16* data;   // the snippet's first frame
    u32 channels, len;       // its channels and length in frames
    u32 pos, step;           // once and noise: the read position and its step a sample, Q16 frames;
                             // tone: the loop's phase, Q32 of the whole loop
    u32 cycles;              // tone: whole cycles in the loop, as a power of two
    i32 gain;                // its gain to the lane's level, Q12
};

struct Voice {
    opxy::SampleView sample;   // the sample stock assigned (the analysis carries its own view)
    u32 cycle;                 // the census cycle this voice last rendered in
    u32 stage;                 // 0 fresh, 1 placing itself, 2 playing
    u32 placed;                // the census cycle of its first block
    u32 rank;                  // how many voices sounded before it: its role (0..3)
    u32 random;                // xorshift: this note's choices
    u32 noise;                 // the lanes' noise
    u32 seed;                  // the note's pattern seed (from its key and rank)
    u32 bits;                  // data: this bar's pattern
    u32 spikes;                // pulse: last step's firings, a bit a lane (bit 31: its counters are set)
    i32 rot[lane_count];       // lanes: each lane's rotation, drifting with chaos
    i32 extra[lane_count];     // lanes: each lane's hit-count drift
    i32 potential[lane_count]; // pulse: each neuron's count of 16ths toward its period
    i32 jump, jump_left;       // break: the chop's read offset and the 16ths it lasts
    i32 crushed[2];            // crush: the held value on each side
    u32 crush_count;
    i32 rest;                  // shred: steps the bass rests (under a block of gated noise) or holds
    i32 bend, bend_ms;         // shred: the next low hit's sweep, from this multiple of its pitch (Q8) over this long (ms x10)
    u32 drop;                  // glitch: samples of silence left in a dropout
    opxy::i16 comb[128 + 64];  // ring: the comb at the key: 128 frames, the first 64 kept again past the end
    u32 comb_at;               // frames written: a whole number of blocks
    i32 comb_low;              // its damping
    u32 comb_key, comb_cycle;  // the key its delay was worked out for, and that delay's cycle in samples
    Lane lane[lane_count];
};

void initialize(Voice& voice, const opxy::SampleView& sample);
void start(Voice& voice);
void render_sample(Voice& voice, const opxy::SampleBlock& block);
}
