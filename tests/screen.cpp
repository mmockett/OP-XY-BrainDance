// Brain Dance's screen checks, compiled for the host and run by
// python sdk.py test --machine <folder>:
// - every frame valid, every dot and link on screen below the header, each
//   link joining its two points, through every control's travel, with keys
//   and without, motion on and reduced, and random settings;
// - the first frame after a reset sets every node; at rest, and after the
//   keys are released, nothing changes;
// - the turn is always smooth: with keys held, in every algorithm and with
//   every control at either end, no point jumps (at most 1.2 px a 60 Hz
//   frame) and no frame stands still;
// - the points are the picture: at the defaults every point shows; links are none
//   at dense 0 with no keys, all with dense 100 and four keys, and never
//   fewer for more density;
// - the sample's hint and Shift overlay, and the sixty steps' captions.
#include "../display.cpp"
#include <cmath>
#include <cstdio>
#include <cstring>

// The track's sample, as the OP-XY hands it to the screen.
static opxy::draw::SampleOverview overview;
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;
using namespace braindance;
long failures = 0, frames = 0;
DisplayState state;
braindance::Frame frame, last;
Input input;
const double defaults[8] = {50.5 / 60, 0.7, 0.4, 0.3, 0.4, 0.7, 0.7, 0.55};
void fail(const char* what) { if (failures++ < 12) std::printf("FAIL %s\n", what); }
bool shown(const Node& n) { return n.x < hidden_x * unit; }
double px(i32 v) { return v / double(unit); }

void check() {
    ++frames;
    if (!valid(frame)) fail("a frame is not valid");
    for (u32 i = 0; i < brain::points; ++i) {
        const Node& n = frame.nodes[brain::first_point + i];
        if (!shown(n)) continue;
        const double r = 1.6 * n.scale / unit + 0.5;
        if (px(n.x) - r < 0 || px(n.x) + r > 480 || px(n.y) - r < 38 || px(n.y) + r > 222) { fail("a dot is drawn off the screen or over the header"); break; }
    }
    // each link runs from its first point to its second, on screen
    for (u32 e = 0; e < brain::links; ++e) {
        const Node& n = frame.nodes[brain::first_link + e];
        if (!shown(n)) continue;
        const Node& a = frame.nodes[brain::first_point + brain::link_a[e]];
        const Node& b = frame.nodes[brain::first_point + brain::link_b[e]];
        if (!shown(a) || !shown(b)) { fail("a link is drawn to a hidden point"); break; }
        const double length = brain::link_length[e] / 4.0 * n.scale / unit;
        const double ex = px(n.x) + length * n.cos / unit, ey = px(n.y) + length * n.sin / unit;
        const double gap = std::sqrt((ex - px(b.x)) * (ex - px(b.x)) + (ey - px(b.y)) * (ey - px(b.y)));
        // never shortened below 15% of its length, so it may overshoot by that much
        if (n.x != a.x || n.y != a.y) { fail("a link doesn't start at its point"); break; }
        if (gap > 1.0 + 0.15 * brain::link_length[e] / 4.0) { fail("a link doesn't reach its other point"); break; }
        if (ex < 0 || ex > 480 || ey < 38 || ey > 222) { fail("a link runs off the screen or over the header"); break; }
    }
}

void draw() { display(state, input, frame); check(); }
void set(const double* c) { for (u32 i = 0; i < 8; ++i) input.slots[i] = i32(c[i] * 2147483647.0); for (u32 i = 0; i < 4; ++i) input.parameters[i] = input.slots[i]; }
u32 changed() {
    u32 n = 0;
    for (u32 i = 0; i < braindance::Frame::node_count; ++i)
        if (std::memcmp(&frame.nodes[i], &last.nodes[i], sizeof(Node)) != 0) ++n;
    last = frame;
    return n;
}
u32 count(u32 first, u32 n) {
    u32 c = 0;
    for (u32 i = 0; i < n; ++i) if (shown(frame.nodes[first + i])) ++c;
    return c;
}

// With keys held and the turn up to speed: the furthest any point moves in a
// frame, at most and at least, over two seconds.
void smooth(const char* name) {
    input.held_notes = 4;
    ++input.note_serial;
    for (u32 k = 0; k < 90; ++k) draw();   // the turn eases in
    double most = 0, least = 1e9;
    for (u32 k = 0; k < 120; ++k) {
        const braindance::Frame before = frame;
        draw();
        double far = 0;
        for (u32 i = 0; i < brain::points; ++i) {
            const Node& a = before.nodes[brain::first_point + i], &b = frame.nodes[brain::first_point + i];
            if (!shown(a) || !shown(b)) continue;
            const double d = std::sqrt((px(b.x) - px(a.x)) * (px(b.x) - px(a.x)) + (px(b.y) - px(a.y)) * (px(b.y) - px(a.y)));
            if (d > far) far = d;
        }
        if (far > most) most = far;
        if (far < least) least = far;
    }
    if (most > 1.2) { std::printf("FAIL %s: a point jumps %.2f px in a frame\n", name, most); ++failures; }
    if (least < 0.02) { std::printf("FAIL %s: a frame stands still (%.3f px)\n", name, least); ++failures; }
    input.held_notes = 0;
}
}

int main() {
    std::memset(&input, 0, sizeof input);
    std::memset(&state, 0, sizeof state);
    input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
    for (u32 i = 0; i < 4; ++i) input.shown[i] = opxy::u8(i);
    set(defaults);
    input.elapsed = 17;   // a 60 Hz frame
    overview.gain = gain_unavailable;
    // the first frame sets every node
    std::memset(&frame, 0xff, sizeof frame);
    display_reset(state);
    draw();
    for (u32 i = 0; i < braindance::Frame::node_count; ++i)
        if (frame.nodes[i].scale <= 0 || frame.nodes[i].scale > 2 * unit) { fail("the first frame left a node unset"); break; }
    last = frame;
    // the points are the picture: all of them show at the defaults
    if (count(brain::first_point, brain::points) != brain::points) fail("not every point shows at the defaults");

    // the sample: with none, the hint shows (its cover off screen) and Shift shows nothing; with one,
    // the cover hides the hint, and Shift dims the brain and says how to swap it, and names what it edits
    {
        const u32 off = u32(hidden_x) * unit;
        if (u32(frame.nodes[brain::hint_cover].x) != off) fail("the hint is covered with no sample loaded");
        input.shift = 1;
        draw();
        if (u32(frame.nodes[brain::overlay_cover].x) != off) fail("Shift shows the swap overlay with no sample to swap");
        overview.generation = 1; overview.assigned = overview.loaded = 1;
        overview.frames = overview.end = 441000; overview.rate = 44100; overview.channels = 1; overview.gain = 0;
        draw();
        if (frame.nodes[brain::hint_cover].x != 0) fail("the hint still shows with a sample loaded");
        if (frame.nodes[brain::overlay_cover].x != 0 || u32(frame.nodes[brain::overlay_icon].x) == off) fail("Shift with a sample doesn't show how to swap it");
        if (std::strcmp(frame.texts[brain::overlay_text].chars, "swap sample") != 0) fail("the overlay doesn't say swap sample");
        bool named = false;
        for (u32 i = 0; i < 16; ++i) if (std::strcmp(frame.texts[i].chars, "window") == 0) named = true;
        if (!named) fail("the header doesn't name what Shift edits");
        input.shift = 0;
        draw();
        if (u32(frame.nodes[brain::overlay_cover].x) != off || frame.texts[brain::overlay_text].chars[0]) fail("the overlay stays after Shift is let go");
    }
    // the first encoder's sixty steps, as the header names them
    {
        static const char* const expect[5] = {"lanes", "lane1", "pulse", "shred", "shrd9"};
        static const u32 at[5] = {0, 1, 40, 50, 59};
        for (u32 k = 0; k < 5; ++k) {
            input.slots[0] = input.parameters[0] = i32((at[k] * 2 + 1) * 17895697u);   // the middle of step at[k] of 60
            draw();
            bool found = false;
            for (u32 i = 0; i < 16; ++i) if (std::strcmp(frame.texts[i].chars, expect[k]) == 0) found = true;
            if (!found) { std::printf("FAIL step %u isn't named %s\n", at[k], expect[k]); ++failures; }
        }
        set(defaults);
    }
    // at rest nothing changes
    for (u32 k = 0; k < 60; ++k) draw();
    changed();
    for (u32 k = 0; k < 120; ++k) { draw(); if (changed()) { fail("the screen changes at rest"); break; } }

    // links: none at dense 0 with no keys, all at dense 100 with four, never fewer for more density
    {
        double c[8];
        std::memcpy(c, defaults, sizeof c);
        c[3] = 0; c[7] = 0;   // no glitch flicker, no crush
        u32 before = 0;
        for (u32 keys = 0; keys <= 4; keys += 2)
            for (u32 step = 0; step <= 10; ++step) {
                c[1] = step / 10.0;
                set(c);
                input.held_notes = keys;
                draw();
                const u32 n = count(brain::first_link, brain::links);
                if (keys == 0 && step == 0 && n != 0) fail("links show at dense 0 with no keys");
                if (step > 0 && n < before) fail("more density shows fewer links");
                if (keys == 4 && step == 10 && n != brain::links) fail("not every link shows at dense 100 with four keys");
                before = n;
            }
        set(defaults);
        input.held_notes = 1;
        draw();
        std::printf("links: %u of %u at the defaults with one key\n", count(brain::first_link, brain::links), brain::links);
        input.held_notes = 0;
    }

    // every control through its travel, with keys and without
    for (u32 slot = 0; slot < 8; ++slot) {
        for (u32 step = 0; step <= 127; ++step)
            for (u32 keys = 0; keys <= 8; keys += 4) {
                input.slots[slot] = i32(step * 16909320u);
                input.held_notes = keys;
                input.reduced_motion = (step & 15) == 3;
                if (keys && step == 0) ++input.note_serial;
                draw();
            }
        set(defaults);
    }
    input.reduced_motion = 0;
    input.held_notes = 0;

    // the turn is always smooth: every algorithm, and every control at either end
    for (u32 algo = 0; algo < 6; ++algo)
        for (u32 variant = 0; variant < 10; variant += 9) {
            double c[8];
            std::memcpy(c, defaults, sizeof c);
            c[0] = (algo * 10 + variant + 0.5) / 60;
            set(c);
            char name[40];
            std::snprintf(name, sizeof name, "algorithm %u variant %u", algo, variant);
            smooth(name);
        }
    for (u32 slot = 1; slot < 8; ++slot)
        for (u32 end = 0; end < 2; ++end) {
            double c[8];
            std::memcpy(c, defaults, sizeof c);
            c[slot] = end;
            set(c);
            static const char* const names[8] = {"algo", "dense", "chaos", "glitch", "tone", "color", "ring", "crush"};
            char name[40];
            std::snprintf(name, sizeof name, "%s at %u", names[slot], end * 100);
            smooth(name);
        }
    {
        const double all[8] = {50.5 / 60, 1, 1, 1, 1, 1, 1, 1};
        set(all);
        smooth("every control up");
    }
    set(defaults);

    // keys released: it eases to a stop, then nothing changes
    input.held_notes = 0;
    for (u32 k = 0; k < 300; ++k) draw();
    changed();
    for (u32 k = 0; k < 60; ++k) { draw(); if (changed()) { fail("the screen still changes after the keys are released"); break; } }

    // random settings and keys for a while
    u32 r = 12345;
    for (u32 n = 0; n < 6000; ++n) {
        r = r * 1664525u + 1013904223u;
        if ((n & 63) == 0) for (u32 i = 0; i < 8; ++i) { r = r * 1664525u + 1013904223u; input.slots[i] = i32(r >> 1); }
        input.held_notes = (r >> 28) % 9;
        input.elapsed = i32(5 + (r >> 20) % 120);
        draw();
    }
    std::printf("%ld frames checked\n", frames);
    if (!failures) std::puts("PASS brain dance screen");
    return failures ? 1 : 0;
}
