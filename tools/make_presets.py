"""Writes Brain Dance's definition.json and presets/*.json from one list.
python tools/make_presets.py"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
# Each algorithm's name (its variant 0) and the stem its variants 1..9 take: sixty steps on the first encoder.
# The stems fit the header's value column in the device's face (display.cpp).
NAMES = [("lanes", "lane"), ("break", "brk"), ("data", "data"), ("eucl", "eucl"), ("pulse", "puls"), ("shred", "shrd")]
ALGORITHMS = [name if v == 0 else f"{stem}{v}" for name, stem in NAMES for v in range(10)]
CONTROLS = [
    # id, label, default (the first preset's), description
    ("algo", "algo", None, "The pattern algorithm"),
    ("dense", "dense", 0.7, "How many hits"),
    ("chaos", "chaos", 0.4, "Mutation, probability and chops"),
    ("glitch", "glitch", 0.3, "Ratchets, rolls, stutters and dropouts"),
    ("tone", "tone", 0.4, "Decay, pitch drop and how much of a step sounds"),
    ("color", "color", 0.7, "Sine to square to bits; FM ratio; the bass's buzz"),
    ("ring", "ring", 0.7, "The gated, staggered reverb, then a comb at the key"),
    ("crush", "crush", 0.55, "Drive into a clip, then bits and sample rate"),
]

# At most 16 (the SDK's limit for a presets folder).
# name, algorithm step (as the header shows it), dense, chaos, glitch, tone, color, ring, crush, role. The first is the default.
PRESETS = [
    ("Shred", "shred", 0.7, 0.4, 0.3, 0.4, 0.7, 0.7, 0.55, "Frantic: the bass sweeping on every 16th in sixes against eights, gated noise in its rests, clipped. The default; try 180 BPM"),
    ("Gate Storm", "shrd4", 0.85, 0.65, 0.6, 0.3, 0.85, 0.85, 0.5, "Shred at full tilt in eights against sixes: blocks of gated reverb, triplet stutters, dropouts"),
    ("Sub Hold", "shrd8", 0.5, 0.75, 0.2, 0.8, 0.3, 0.45, 0.4, "Longer, rounder sweeps that keep landing and holding; fewer clicks"),
    ("Comb Teeth", "shrd2", 0.6, 0.3, 0.4, 0.5, 0.5, 0.95, 0.3, "Wide leaps in sevens, the comb ringing at the key: a metallic buzz with every harmonic"),
    ("Threes", "shrd3", 0.75, 0.35, 0.45, 0.35, 0.6, 0.6, 0.5, "Cycles of three against four, five and seven, ratchets in fifths"),
    ("Long Count", "shrd9", 0.6, 0.5, 0.35, 0.55, 0.45, 0.75, 0.45, "Twelve against nine against eight: long cycles, many blocks, clicks and blips an octave up"),
    ("Synapse", "lanes", 0.5, 0.3, 0.25, 0.4, 0.3, 0.35, 0.25, "Autechre-style polymeter: four lanes drifting against each other, through the gated space"),
    ("Amen Lobe", "break", 0.55, 0.35, 0.35, 0.45, 0.2, 0.4, 0.3, "A chopped two-bar break, rolls on the snare, a gated room on it"),
    ("Two Step", "brk1", 0.5, 0.3, 0.3, 0.5, 0.3, 0.5, 0.3, "A two-step: the kick skipping, snares on the backbeat, offbeat ride"),
    ("Drill", "brk4", 0.8, 0.6, 0.75, 0.25, 0.4, 0.5, 0.55, "Drill 'n' bass: a jungle chop shredded, buzz rolls, driven hard"),
    ("Datamatic", "data", 0.55, 0.1, 0.3, 0.2, 0.7, 0.2, 0.0, "Ikeda-style: sine pulses and bit noise under hard gates, a bar repeated"),
    ("Pulse Five", "data3", 0.6, 0.2, 0.4, 0.25, 0.8, 0.35, 0.2, "A bar of twenty 32nds pulsing in fives, bits and beeps"),
    ("Odd Wheel", "eucl9", 0.6, 0.3, 0.3, 0.4, 0.5, 0.5, 0.35, "Euclidean wheels of 13, 11, 7 and 5 turning against each other"),
    ("Spike Train", "pulse", 0.55, 0.25, 0.3, 0.35, 0.5, 0.5, 0.3, "Pacemaker neurons on the grid, each setting off the next: interlocking pulses that repeat"),
    ("Overload", "puls6", 0.85, 0.6, 0.85, 0.2, 0.85, 0.7, 0.75, "The network at double time, nudged two steps at a go: dense, crushed to bits, glitching"),
    ("Resonance", "lane8", 0.45, 0.3, 0.2, 0.6, 0.15, 0.9, 0.15, "Long lanes of 24 and 16 through the comb: the beat sings the key"),
]


def algo_value(name):
    i = ALGORITHMS.index(name)
    return round((i + 0.5) / len(ALGORITHMS), 4)


def definition():
    params = []
    for cid, label, default, _ in CONTROLS:
        if cid == "algo":
            params.append({"id": cid, "label": label, "default": algo_value(PRESETS[0][1]),
                           "mapping": {"kind": "enum", "labels": ALGORITHMS}, "display": "enum"})
        else:
            params.append({"id": cid, "label": label, "default": default,
                           "mapping": {"kind": "linear", "minimum": 0, "maximum": 1}, "display": "percent"})
    presets = {}
    for p in PRESETS:
        values = [algo_value(p[1])] + list(p[2:9])
        presets[p[0]] = {c[0]: v for c, v in zip(CONTROLS, values)}
    return {
        "schema": 1,
        "id": "braindance",
        "name": "Brain Dance",
        "parameters": params,
        "presets": presets,
        "resources": {
            "voiceCapacity": 27,
            "stereoOutput": 1,
            # harvest.h: about 14 KiB for a 20 s sample (a word every 256 frames), so
            # about eight samples at once. The firmware's samples share 2 MiB, so
            # Brain Dance can't be built beside a machine that takes it all (Timestretch).
            "sampleAnalysis": 131072,
            # Shift + the encoders: where it harvests the sample (start, both markers, end) and how loud
            "stockShift": ["start", "window", "gain", "end"],
            "display": DISPLAY,
        },
    }


# Matches display.h (Frame) and DisplayState's size; make_panel.py checks it too.
DISPLAY = {"stateBytes": 0, "stateAlignment": 4, "paths": 0, "points": 0, "nodes": 0, "texts": 18, "palette": 16}


def preset_file(p):
    values = [algo_value(p[1])] + list(p[2:9])
    return {
        "engine": {
            "bendrange": 8191, "highpass": 0,
            "modulation": {k: {"amount": 16384, "target": 0} for k in ("aftertouch", "modwheel", "pitchbend", "velocity")},
            "params": [int(round(v * 32767)) for v in values],
            "playmode": "poly", "portamento.amount": 0, "portamento.type": 0, "transpose": 0,
            "tuning.root": 0, "tuning.scale": 0, "velocity.sensitivity": 0, "volume": 16384, "width": 0,
        },
        # Held keys keep playing; the release lets the last hits and the ring finish.
        "envelope": {"amp": {"attack": 0, "decay": 32767, "release": 24000, "sustain": 32767},
                     "filter": {"attack": 0, "decay": 0, "release": 0, "sustain": 32767}},
        "fx": {"active": False, "params": [0, 0, 0, 0, 0, 32767, 0, 0], "type": "svf"},
        "lfo": {"active": False, "params": [0] * 8, "type": "value"},
        "octave": 0, "platform": "OP-XY", "regions": [], "type": "braindance", "version": 4,
    }


def main():
    import display_budget
    DISPLAY.update(display_budget.budget())
    d = definition()
    (ROOT / "definition.json").write_text(json.dumps(d, indent=2) + "\n")
    out = ROOT / "presets"
    out.mkdir(exist_ok=True)
    for f in out.glob("*.json"):
        f.unlink()
    for p in PRESETS:
        name = "default" if p is PRESETS[0] else p[0].lower().replace(" ", "-").replace("'", "")
        (out / f"{name}.json").write_text(json.dumps(preset_file(p), indent=2) + "\n")
    rows = ["| Preset | Algorithm | Plays | dense / chaos / glitch / tone / color / ring / crush |", "| --- | --- | --- | --- |"]
    for p in PRESETS:
        rows.append(f"| {p[0]} | {p[1]} | {p[9]} | " + " / ".join(str(int(round(v * 100))) for v in p[2:9]) + " |")
    (ROOT / "docs" / "presets.md").write_text("\n".join(rows) + "\n")


if __name__ == "__main__":
    import sys
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    main()
