// Brain Dance's screen (README, "The screen"): the brain as a cloud of 270
// points, turning, and nothing else; links between near neighbours appear as
// the playing gets denser, under the points and dimmer, so sparse is a cloud
// and dense a cloud laced together. Its red and blue clusters of neurons
// signal while keys are held, each signal a wave through the cloud. The screen
// can't hear the engine, so it follows the same controls: dense and the keys
// held show more links and signal more often; ring sets how far a wave
// travels; tone sizes the dots and widens the wave; chaos turns it faster;
// glitch makes points and links flicker; crush strips the far side away, to
// a few stark points; color tints the idle clusters.
//
// The turn is always smooth. Every point is placed from the turn alone, every
// frame, in sixteenths of a pixel: no control and no algorithm steps it,
// snaps it or shifts it sideways. The OP-XY redraws all of it comfortably:
// about 450 thin lines and small dots changing every frame, on the device.
#include "display.h"
#include <opxy/sample_overview.hpp>

// The sample's Shift overlay, shared with Grains, Ghost and Recur (sample_view.h says how to include it).
namespace braindance { namespace sample_view {
#include "sample_view.h"
} }

namespace braindance {
namespace {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

const i32 tilt = -140;                       // seen a little from above, tenths of a degree
const i32 wave_speed = 150;                  // hundredths of a model unit a second: a wave crosses the brain in about a second
// The algorithm's caption: its name for variant 0, then a four-letter stem and the variant's digit.
// Measured in the device's face (Univers Next Pro Light, by its advance widths): the widest, data0, is 50.8 of the
// column's 53 px; "shred9" would be 61.4, "shed9" 54.5 and plain "euclid" 53.6, so those are cut.
const char* const names[6] = {"lanes", "break", "data", "eucl", "pulse", "shred"};
const char* const stems[6] = {"lane", "brk", "data", "eucl", "puls", "shrd"};

inline u32 next(u32& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
inline u32 hash(u32 x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline i32 q15(i32 control) { return control < 0 ? 0 : control >> 16; }
// The first encoder's step, 0..59: the algorithm (its tens) and the variant (its units).
inline u32 step_of(const Input& input) {
    const u32 a = (static_cast<u32>(q15(input.slots[0])) * 60u) >> 15;
    return a > 59 ? 59 : a;
}
inline u32 algorithm(const Input& input) { return step_of(input) / 10; }
inline u32 isqrt(u32 v) {
    u32 r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}
inline bool red_cluster(u32 c) { return c == 0 || c == 2; }   // clusters 1 and 3 are red, 2 and 4 blue

// ---- Shift (resources.stockShift): the encoders turn the stock sampler's start,
// both markers together (the window), gain and end: where Brain Dance harvests
// the sample, and how loud. While Shift is held the header names them, as
// Ghost's does (its captions.h): times in the sample, the gain in stock's steps.
const char* const shift_titles[4] = {"start", "window", "gain", "end"};
inline char* put(char* out, u32 value) {
    char digits[10];
    u32 n = 0;
    do { digits[n++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    while (n) *out++ = digits[--n];
    *out = '\0';
    return out;
}
inline void append(char* value, const char* unit) {
    u32 at = 0;
    while (at < header_capacity - 1 && value[at]) ++at;
    for (u32 i = 0; unit[i] && at < header_capacity - 1; ++i) value[at++] = unit[i];
    value[at] = '\0';
}
// Frames as seconds: 0.62s, 12.4s, 123s.
inline void seconds(char* value, u32 frames, u32 rate) {
    const u32 per = rate / 100 ? rate / 100 : 1, hundredths = frames / per;
    char* out = value;
    if (hundredths < 1000) { out = put(out, hundredths / 100); *out++ = '.'; *out++ = static_cast<char>('0' + hundredths / 10 % 10); *out++ = static_cast<char>('0' + hundredths % 10); *out = '\0'; }
    else if (hundredths < 10000) { out = put(out, hundredths / 100); *out++ = '.'; *out++ = static_cast<char>('0' + hundredths / 10 % 10); *out = '\0'; }
    else put(out, hundredths / 100);
    append(value, "s");
}
inline void shift_row(const Input& input, const opxy::u8 (&pages)[2][4], const SampleOverview& o, const char* (&titles)[8],
                      char (&values)[8][header_capacity]) {
    if (!input.shift) return;
    u32 page = 0;   // as page_header finds it
    for (u32 col = 0; col < 4; ++col)
        if (pages[1][col] != pages[0][col] && input.shown[col] == pages[1][col]) page = 1;
    const bool known = o.assigned && o.loaded && o.frames && o.rate;
    const u32 end = o.end && o.end <= o.frames ? o.end : o.frames, begin = o.begin < end ? o.begin : 0;
    const u32 marks[4] = {begin, end - begin, 0, end};
    for (u32 col = 0; col < 4; ++col) {
        const u32 slot = pages[page][col];
        titles[slot] = shift_titles[col];
        if (col == 2) {
            values[slot][0] = '\0';
            if (o.gain == gain_unavailable) titles[slot] = "";
            else { values[slot][0] = o.gain < 0 ? '-' : '+'; put(values[slot] + 1, static_cast<u32>(o.gain < 0 ? -o.gain : o.gain)); }
        } else if (known) seconds(values[slot], marks[col], o.rate);
        else copy(values[slot], header_capacity, "-");
    }
}
}

inline void display_reset(DisplayState& state) {
    state.header.reset();
    state.notes.reset();
    state.fresh = 1;
    if (!state.random) state.random = 0x2545f491u;
    state.pace = 0;
    for (u32 c = 0; c < 4; ++c) { state.age[c] = 4096; state.due[c] = static_cast<i32>(c) * 16384; }
}

inline void display(DisplayState& state, const Input& input, Frame& frame) {
    // ---- the header: both pages, the algorithm by name
    const SampleOverview& overview = *opxy_sample_overview();
    const char* titles[8] = {"algo", "dense", "chaos", "glitch", "tone", "color", "ring", "crush"};
    char values[8][header_capacity];
    {
        const u32 step = step_of(input), variant = step % 10;
        copy(values[0], header_capacity, variant ? stems[step / 10] : names[step / 10]);
        if (variant) {
            u32 at = 0;
            while (values[0][at]) ++at;
            values[0][at] = static_cast<char>('0' + variant);
            values[0][at + 1] = '\0';
        }
    }
    for (u32 i = 1; i < 8; ++i) header_amount(values[i], header_capacity, input.slots[i]);
    const opxy::u8 pages[2][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}};
    shift_row(input, pages, overview, titles, values);
    page_header(state.header, input, frame, titles, values, brain::first_header, pages);

    state.notes.update(input);
    const u32 held = input.held_notes <= 128 ? input.held_notes : 0;
    const u32 keys = held > 4 ? 4 : held;
    const bool still = input.reduced_motion != 0;
    const i32 dt = clamp(input.elapsed, 0, 102);                       // Q10 s, at most 0.1 s
    const u32 algo = algorithm(input);
    const i32 dense = q15(input.slots[1]), chaos = q15(input.slots[2]), glitch = q15(input.slots[3]);
    const i32 tone = q15(input.slots[4]), color = q15(input.slots[5]), ring = q15(input.slots[6]), crush = q15(input.slots[7]);
    ++state.frames;

    // ---- the turn: still at rest; turning while keys are held, faster with keys and chaos, easing in and out.
    // Only its speed changes, and only gradually: nothing steps or snaps it.
    state.pace = held ? clamp(state.pace + dt * 4, 0, 1024) : clamp(state.pace - dt, 0, 1024);
    const i32 speed = (state.pace * (50 + (chaos * 150 >> 15) + static_cast<i32>(keys) * 25)) >> 10;   // tenths of a degree a second
    if (!still) state.angle = (state.angle + speed * dt) % (3600 << 10);
    const Turn spin = rotation(state.angle >> 10), lean = rotation(tilt);
    // the fraction of a tenth of a degree, so the slowest turn still moves every frame
    const i32 fine = state.angle & 1023;

    // ---- the signals: one cluster a key, each at density's rate; a signal lights its
    // cluster, then spreads through the cloud as a wave, as far as ring lets it
    const i32 rate = 1 + (dense * 5 >> 15) + static_cast<i32>(keys);   // signals a second
    const i32 hold = 61 + (tone * 184 >> 15);                            // Q10 s the cluster stays lit: 0.06 to 0.24
    const i32 reach = 25 + (ring * 110 >> 15);                           // how far a wave travels, hundredths of a unit
    const i32 width = 6 + (tone * 10 >> 15);                             // the wave's front, as thick
    bool started = false;                                   // one signal starts a frame; the next waits
    i32 front[4];                                           // each wave's front, hundredths of a unit; beyond reach: none
    for (u32 c = 0; c < 4; ++c) {
        state.age[c] = clamp(state.age[c] + dt, 0, 4096);
        front[c] = state.age[c] * wave_speed >> 10;
        if (front[c] > reach + width) front[c] = -1000;
        if (still || c >= keys) { state.due[c] = static_cast<i32>(c) * 16384; continue; }
        state.due[c] += rate * dt * 64;                     // Q16
        // pulse: a cluster tends to signal just after the one before it
        if (algo == 4 && state.age[(c + 3) & 3] < 122 && (next(state.random) & 15) == 0) state.due[c] += 32768;
        if (state.due[c] >= 65536 && !started) {
            started = true;
            state.due[c] -= 65536;
            if (state.due[c] > 65536) state.due[c] = 65535;
            ++state.flashes[c];
            state.age[c] = 0;
            front[c] = 0;
        }
    }

    // ---- crush: up to 60% the image only grows starker (as the sound only drives); above it the far
    // side is stripped away, points and links further than this hidden, as the sound loses its bits
    const i32 over = crush > 19661 ? crush - 19661 : 0;
    const i32 cut = over * 100 / 13106;                     // of 140: at 100, only the nearest two fifths stay
    const i32 dot = 780 + (tone * 300 >> 15);               // Q10 scale: tone grows the dots a little, never to blobs

    // ---- the points
    for (u32 i = 0; i < brain::points; ++i) {
        const i32 x = brain::x[i], y = brain::y[i], z = brain::z[i];
        // The turn to a 1024th of a tenth of a degree, so the slowest turn still moves a little every
        // frame: the whole tenths by the table, the rest as a small turn more (in radians: fine / 1024
        // tenths of a degree is fine / 573 of a 1024th). Everything stays Q17 (Q7 x Q10) to the pixel.
        i32 rx = x * spin.cos + z * spin.sin, rz = z * spin.cos - x * spin.sin;
        rx += (rz >> 10) * fine / 573;
        rz -= (rx >> 10) * fine / 573;
        const i32 sy = y * lean.cos - (((rz >> 5) * lean.sin) >> 5);
        const i32 depth = (y * lean.sin + (((rz >> 5) * lean.cos) >> 5)) >> 10;   // toward the viewer: positive, Q7
        const i32 px = clamp((brain::centre_x << 4) + ((rx >> 7) * brain::size_px >> 6), 4 << 4, 476 << 4);   // Q4 px
        const i32 py = clamp((brain::centre_y << 4) - ((sy >> 7) * brain::size_px >> 6), 42 << 4, 218 << 4);
        const i32 near = clamp(depth + 70, 0, 140);                 // 0 far .. 140 near
        state.px[i] = static_cast<opxy::i16>(px);
        state.py[i] = static_cast<opxy::i16>(py);
        state.near[i] = static_cast<opxy::u8>(near);
        const u32 node = brain::first_point + i;
        if (near < cut) { hide(frame, node); continue; }

        const u32 kind = brain::kind[i];
        // greys 5..10 by depth: the points are the picture, so even the far ones show; crush makes them starker
        u32 shade = static_cast<u32>(5 + near * 5 / 140 + (crush * 3 >> 15));
        if (shade > 10) shade = 10;
        if (kind) {
            const u32 c = kind - 1;
            const bool redish = red_cluster(c);
            // a signal lights about two thirds of its cluster
            const u32 pick = hash(i * 0x9e3779b9u + state.flashes[c] * 0x85ebca6bu);
            const bool lit = state.age[c] < hold && (pick & 3) != 0;
            // color leans the clusters: low color warms the blue clusters' idle tint, high cools the red
            const bool hot = lit || (c >= keys && ((color > 26000 && !redish) || (color < 6500 && redish)) && (i & 1));
            shade = redish ? (hot ? brain::red : brain::dim_red) : (hot ? brain::blue : brain::dim_blue);
            if (lit && near > 110 && (pick & 0x18) == 0) shade = brain::white;   // the nearest flash white-hot
        } else {
            // a wave passing: the points on its front turn white
            for (u32 c = 0; c < 4; ++c) {
                const i32 gap = static_cast<i32>(brain::point_far[i][c]) - front[c];
                if (gap > -width && gap < width && brain::point_far[i][c] <= reach) { shade = brain::white; break; }
            }
            // glitch: grey points flicker white now and then, each on its own frame
            if (held && !still && glitch > 3276) {
                const u32 tw = hash(brain::twinkle[i] + ((state.frames + brain::twinkle[i]) >> 2) * 0x27d4eb2du) & 0x3ff;
                if (static_cast<i32>(tw) < (glitch * 40 >> 15)) shade = brain::white;
            }
        }
        const i32 scale = clamp(dot * (800 + near * 4) / 1024, 300, 2048);
        place(frame, node, px * (unit >> 4), py * (unit >> 4), scale, shade, fill_and_stroke);
    }

    // ---- the links: more of them the denser the playing (dense, and each key held), in brain.h's order
    const i32 amount = clamp((dense >> 1) + static_cast<i32>(keys) * 4096, 0, 32767);
    const u32 shown = static_cast<u32>(amount + 1) * brain::links >> 15;
    for (u32 e = 0; e < brain::links; ++e) {
        const u32 node = brain::first_link + e;
        const u32 a = brain::link_a[e], b = brain::link_b[e];
        const i32 near = (state.near[a] + state.near[b]) >> 1;
        bool on = e < shown && state.near[a] >= cut && state.near[b] >= cut;
        // glitch: a link drops out now and then
        if (on && held && !still && glitch > 3276) {
            const u32 fl = hash(e * 0x9e3779b9u + (state.frames >> 3) * 0x27d4eb2du) & 0x3ff;
            if (static_cast<i32>(fl) < (glitch * 160 >> 15)) on = false;
        }
        if (!on) { hide(frame, node); continue; }
        // greys 3..6 by depth, dimmer than the points; a wave lights them in its cluster's colour near it, white further on
        u32 shade = static_cast<u32>(3 + near * 3 / 140);
        for (u32 c = 0; c < 4; ++c) {
            const i32 far = brain::link_far[e][c], gap = far - front[c];
            if (gap > -width && gap < width && far <= reach) { shade = far < 30 ? (red_cluster(c) ? brain::red : brain::blue) : 9u; break; }
        }
        const i32 dx = state.px[b] - state.px[a], dy = state.py[b] - state.py[a];   // Q4 px
        // its direction from Q8 when short, so a sub-pixel link still turns by a true unit vector
        const i32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        const u32 up = ax < 2048 && ay < 2048 ? 4u : 0u;
        const i32 ex = dx << up, ey = dy << up;
        const u32 sq = static_cast<u32>(ex * ex) + static_cast<u32>(ey * ey);
        u32 root = isqrt(sq);
        if ((root + 1) * (root + 1) - sq < sq - root * root) ++root;   // rounded
        const i32 length = static_cast<i32>(root >> up);
        Turn along = {unit, 0};
        if (root > 0) { along.cos = ex * unit / static_cast<i32>(root); along.sin = ey * unit / static_cast<i32>(root); }
        // authored a little longer than its full length (Q2 px): shortened as it turns away, never to nothing
        const i32 scale = clamp(length * 256 / static_cast<i32>(brain::link_length[e]), 154, 2048);
        place(frame, node, state.px[a] * (unit >> 4), state.py[a] * (unit >> 4), scale, shade, fill_and_stroke);
        opxy::draw::turn(frame, node, along);
    }

    // ---- the sample: with none, the hint in the corner says how to load one (its cover
    // waits off screen); with one, the cover hides the hint, and holding Shift dims the
    // brain and says how to swap it. The overlay is centred between the header and the screen's foot.
    if (overview.assigned) place(frame, brain::hint_cover, 0, 0, unit, 0, fill_and_stroke);
    else hide(frame, brain::hint_cover);
    const sample_view::SampleSlot foot = {0, 480 * unit, 220 * unit, 0, 0, 0, 0};
    sample_view::place_shift_overlay(frame, brain::overlay_cover, brain::overlay_icon, brain::overlay_text, foot,
                                     input.shift != 0 && overview.assigned != 0);
    state.fresh = 0;
}
}
