// The web mockup's bridge: Brain Dance's own engine (sample.cpp) and screen
// (display.cpp), compiled to WebAssembly unchanged, with eight voices as the
// OP-XY would play them on one track. web/build.sh builds it.
#include "../sample.cpp"
#include "../display.cpp"

// The screen's view of the track's sample (the OP-XY supplies this; here the page sets it).
static opxy::draw::SampleOverview overview;
extern "C" const opxy::draw::SampleOverview* opxy_sample_overview() { return &overview; }

extern "C" {
void* memset(void* d, int c, unsigned long n) {
    unsigned char* p = static_cast<unsigned char*>(d);
    while (n--) *p++ = static_cast<unsigned char>(c);
    return d;
}
void* memcpy(void* d, const void* s, unsigned long n) {
    unsigned char* p = static_cast<unsigned char*>(d);
    const unsigned char* q = static_cast<const unsigned char*>(s);
    while (n--) *p++ = *q++;
    return d;
}
}

namespace {
using opxy::i32;
using opxy::u32;
braindance::Voice voices[8];
i32 params[8];
u32 pitch[64];
i32 out[128];
braindance::DisplayState screen;
// A loaded sample (web/index.html: drop a file), analysed as the OP-XY would on load.
const u32 sample_capacity = 882000;   // 20 s at 44.1 kHz, the OP-XY's longest
opxy::i16 pcm[sample_capacity * 2];
u32 harvest_words[braindance::harvest::result_words + (sample_capacity + braindance::harvest::hop - 1) / braindance::harvest::hop];
braindance::Preparation preparation;
opxy::SampleAnalysis harvested;
bool have_sample = false;
braindance::Frame frame;
opxy::draw::Input input;
u32 page_now;
}

extern "C" {
// Controls: the eight Q31 words.
i32* bd_params() { return params; }
i32* bd_out() { return out; }
void bd_start(u32 v) {
    braindance::initialize(voices[v], have_sample ? harvested.sample : opxy::SampleView{0, 0, 44100, 1});
    braindance::start(voices[v]);
}
// One 64-sample block of voice v at increment inc; stereo into bd_out().
void bd_render(u32 v, u32 inc, u32 bpm_x10, u32 ticks96, u32 playing) {
    for (u32 i = 0; i < 64; ++i) pitch[i] = inc;
    opxy::SampleBlock block = opxy::SampleBlock();
    block.output = out;
    block.parameters = params;
    block.pitch = pitch;
    block.transport.bpm_x10 = bpm_x10;
    block.transport.ticks96 = ticks96;
    block.transport.playing = playing != 0;
    block.output_channels = 2;
    if (have_sample) {
        block.analysis = &harvested;
        block.region.begin = 0;
        block.region.end = harvested.sample.frames;
        block.region.level = opxy::sample_unity_level;
    }
    braindance::render_sample(voices[v], block);
}
// The sample: the page writes 16-bit frames (stereo interleaved) here, then
// bd_load analyses them in the SDK's 4096-frame steps; 0 frames clears it.
// Returns the snippets found, or -1 if the analysis refused the sample.
opxy::i16* bd_sample_data() { return pcm; }
u32 bd_sample_capacity() { return sample_capacity; }
i32 bd_load(u32 frames, u32 rate, u32 channels) {
    have_sample = false;
    if (!frames) return 0;
    if (frames > sample_capacity) frames = sample_capacity;
    const opxy::SampleView view = {pcm, frames, rate, channels};
    const u32 words = braindance::analysis_words(view);
    if (!words || words > sizeof harvest_words / sizeof harvest_words[0]) return -1;
    preparation = braindance::Preparation();
    if (!braindance::prepare_begin(preparation, view, harvest_words, words)) return -1;
    u32 cursor = 0;
    opxy::PrepareStatus status;
    while ((status = braindance::prepare_step(preparation, 4096, cursor)) == opxy::prepare_running) {}
    if (status != opxy::prepare_done || !braindance::prepared(preparation, harvested)) return -1;
    have_sample = true;
    return static_cast<i32>(braindance::harvest::count_of(harvest_words));
}
// The snippets: their records (harvest.h) after the 4-word header.
const u32* bd_harvest() { return harvest_words; }

// A lane's level (Q31) and the hits so far, for the visuals.
i32 bd_lane(u32 v, u32 k) { return voices[v].lane[k].amp; }
u32 bd_rank(u32 v) { return voices[v].rank; }
u32 bd_strikes() { return braindance::engine::strikes; }

// The screen: one frame of the OP-XY display.
void bd_display(i32 elapsed_q10, u32 held, u32 serial, u32 reduced) {
    static bool reset = false;
    if (!reset) {
        braindance::display_reset(screen);
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
        reset = true;
    }
    for (u32 i = 0; i < 8; ++i) input.slots[i] = params[i];
    for (u32 i = 0; i < 4; ++i) {
        input.parameters[i] = params[i + (page_now ? 4 : 0)];
        input.shown[i] = static_cast<opxy::u8>(i + (page_now ? 4 : 0));
    }
    input.elapsed = elapsed_q10;
    input.held_notes = held;
    input.note_serial = serial;
    input.reduced_motion = reduced;
    braindance::display(screen, input, frame);
}
void bd_page(u32 page) { page_now = page ? 1 : 0; }
// The screen's sample and Shift: frames 0 is no sample (the hint shows); Shift with one shows how to swap it.
void bd_overview(u32 frames, u32 rate) {
    overview = opxy::draw::SampleOverview();
    overview.gain = frames ? 0 : opxy::draw::gain_unavailable;
    if (!frames) return;
    overview.generation = 1;
    overview.assigned = overview.loaded = 1;
    overview.frames = overview.end = frames;
    overview.rate = rate;
    overview.channels = 1;
}
void bd_shift(u32 held) { input.shift = held ? 1 : 0; }
braindance::Frame* bd_frame() { return &frame; }
u32 bd_frame_nodes() { return braindance::Frame::node_count; }
u32 bd_node_bytes() { return sizeof(opxy::draw::Node); }
u32 bd_nodes_offset() { return static_cast<u32>(reinterpret_cast<const char*>(&frame.nodes[0]) - reinterpret_cast<const char*>(&frame)); }
u32 bd_texts_offset() { return static_cast<u32>(reinterpret_cast<const char*>(&frame.texts[0]) - reinterpret_cast<const char*>(&frame)); }
u32 bd_text_bytes() { return sizeof(opxy::draw::Text); }
u32 bd_paths_offset() { return static_cast<u32>(reinterpret_cast<const char*>(&frame.paths[0]) - reinterpret_cast<const char*>(&frame)); }
}
