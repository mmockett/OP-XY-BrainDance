// Renders Brain Dance on the host, as the OP-XY would sum its voices, to a
// stereo 16-bit WAV: tools/render <step 0..59: the algorithm x 10 + its variant> <keys> <bars> <bpm> <playing 0/1> <out.wav>
//   [dense chaos glitch tone color ring crush] (0..1 each)
// Keys are held from the start, a fifth and an octave apart, entering a beat apart.
#include "../sample.cpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: render algo keys bars bpm playing out.wav [8 controls]\n"); return 2; }
    const int algo = std::atoi(argv[1]), keys = std::atoi(argv[2]), bars = std::atoi(argv[3]);
    const double bpm = std::atof(argv[4]);
    const bool playing = std::atoi(argv[5]) != 0;
    double c[8] = {0, 0.5, 0.3, 0.3, 0.4, 0.3, 0.0, 0.0};
    c[0] = (algo + 0.5) / 60.0;
    for (int i = 0; i < 7 && 7 + i < argc; ++i) c[1 + i] = std::atof(argv[7 + i]);
    opxy::i32 params[8];
    for (int i = 0; i < 8; ++i) params[i] = opxy::i32(c[i] * 2147483647.0);
    const double fs = 44100;
    const long total = long(bars * 4 * 60.0 / bpm * fs) / 64 * 64;
    const int midi[8] = {48, 55, 60, 67, 63, 70, 72, 51};
    std::vector<braindance::Voice> voices(keys);
    std::vector<long> enter(keys);
    for (int k = 0; k < keys; ++k) enter[k] = long(k * 60.0 / bpm * fs) / 64 * 64;
    std::vector<bool> begun(keys, false);
    std::vector<short> pcm(total * 2);
    opxy::i32 out[128];
    opxy::u32 pitch[64];
    for (long b = 0; b < total; b += 64) {
        double mixl[64] = {0}, mixr[64] = {0};
        for (int k = 0; k < keys; ++k) {
            if (b < enter[k]) continue;
            if (!begun[k]) { braindance::initialize(voices[k], opxy::SampleView{0, 0, 44100, 1}); braindance::start(voices[k]); begun[k] = true; }
            const double f = 440 * std::pow(2.0, (midi[k] - 69) / 12.0);
            for (int i = 0; i < 64; ++i) pitch[i] = opxy::u32(f / fs * 4294967296.0);
            opxy::SampleBlock blk = opxy::SampleBlock();
            blk.output = out; blk.parameters = params; blk.pitch = pitch;
            blk.transport.bpm_x10 = opxy::u32(bpm * 10 + 0.5);
            blk.transport.playing = playing;
            blk.transport.ticks96 = playing ? opxy::u32(b / fs * bpm / 60 * 96) : 0;
            blk.output_channels = 2;
            braindance::render_sample(voices[k], blk);
            for (int i = 0; i < 64; ++i) { mixl[i] += out[2 * i] / 2147483648.0; mixr[i] += out[2 * i + 1] / 2147483648.0; }
        }
        for (int i = 0; i < 64; ++i) {
            const double l = std::fmax(-1, std::fmin(1, mixl[i])), r = std::fmax(-1, std::fmin(1, mixr[i]));
            pcm[2 * (b + i)] = short(std::lround(l * 32767));
            pcm[2 * (b + i) + 1] = short(std::lround(r * 32767));
        }
    }
    FILE* f = std::fopen(argv[6], "wb");
    const unsigned data = unsigned(pcm.size() * 2), rate = 44100, byte_rate = rate * 4;
    const unsigned riff = 36 + data;
    const unsigned short fmt = 1, ch = 2, align = 4, bits = 16;
    const unsigned fmt_len = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f); std::fwrite(&byte_rate, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&data, 4, 1, f); std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return 0;
}
