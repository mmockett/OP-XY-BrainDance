#pragma once
#include <opxy/page_header.hpp>
#include "brain.h"

namespace braindance {
// Matches resources.display in definition.json: paths, points, nodes, texts, palette.
// Nodes 0..7 are the header's bars, then the brain's links and its points, then the
// sample hint's cover and the Shift overlay (brain.h). Text slots 0..15 are the
// header's, 16 and 17 the overlay's.
using Frame = opxy::draw::Frame<0, 0, brain::nodes, 18, 16>;

struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    opxy::u32 fresh;          // 1 until the first frame after a reset has set every node
    opxy::u32 random;         // xorshift: the signals
    opxy::i32 angle;          // the brain's turn, Q10 tenths of a degree
    opxy::i32 pace;           // Q10: 1 while keys are held, easing to 0 after
    opxy::i32 age[4];         // Q10 s since each cluster's signal started
    opxy::u32 flashes[4];     // each cluster's signals: which of its neurons light
    opxy::i32 due[4];         // Q16 signals owed to each cluster
    opxy::u32 frames;
    opxy::i16 px[brain::points], py[brain::points];   // each point as drawn, Q4 px
    opxy::u8 near[brain::points];                     // and how near, 0 far .. 140 near
};
}
