"""Measures a Brain Dance render: level, onsets, how many land on the 32nd
grid, and how busy it is. Needs numpy and scipy.
python tools/analyze.py out.wav bpm"""
import sys
import wave

import numpy as np


def load(path):
    with wave.open(path) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2) / 32768.0
    return x


def onsets(mono, fs=44100, hop=64):
    from scipy.signal import butter, sosfilt
    mono = sosfilt(butter(2, 300, "highpass", fs=fs, output="sos"), mono)
    frames = len(mono) // hop
    env = np.sqrt(np.mean(mono[: frames * hop].reshape(frames, hop) ** 2, axis=1) + 1e-12)
    env = np.maximum(env, np.concatenate([[env[0]], env[:-1]]) * 0.85)
    db = 20 * np.log10(env)
    rise = np.diff(db, prepend=db[0])
    found, last = [], -10**9
    for i in range(1, frames):
        if rise[i] > 9 and db[i] > -50 and (i - last) * hop > fs * 0.02:
            found.append(i * hop)
            last = i
    return np.array(found)


def report(path, bpm):
    x = load(path)
    fs = 44100
    mono = x.mean(axis=1)
    peak = np.abs(x).max()
    rms = np.sqrt(np.mean(mono**2))
    on = onsets(mono)
    s32 = fs * 60 / bpm / 8
    err = np.abs(((on + s32 / 2) % s32) - s32 / 2) if len(on) else np.array([0])
    grid = float(np.mean(err < 0.004 * fs + 64)) if len(on) else 0
    seconds = len(mono) / fs
    width = np.sqrt(np.mean((x[:, 0] - x[:, 1]) ** 2)) / (np.sqrt(np.mean(mono**2)) + 1e-9)
    return dict(peak_db=round(20 * np.log10(peak + 1e-9), 1), rms_db=round(20 * np.log10(rms + 1e-9), 1),
                onsets_per_s=round(len(on) / seconds, 1), on_grid=round(grid, 2), width=round(float(width), 2))


if __name__ == "__main__":
    print(report(sys.argv[1], float(sys.argv[2])))
