"""Writes Brain Dance's screen: the brain as a cloud of points and the links
between them (brain.h), and the artwork the display moves (panel.svg.fragment).
python tools/make_panel.py    Needs numpy.

The brain: two hemispheres, the temporal lobes, the cerebellum and the
brainstem, with folds, sampled on their surface and a few inside, then thinned
evenly: 270 points, drawn as dots large and bright enough to be the picture.
Four clusters of neurons, two red and two blue, sit in the frontal and parietal
lobes and deep inside, as in the picture the machine is drawn from.

Links: 180 short lines, each between two near neighbours, no point with more
than three. They are drawn under the dots and dimmer, and the display shows
more of them as the playing gets denser (in the order listed here, which is
shuffled, so they spread evenly): sparse is a cloud of points, dense a cloud
laced together.

A cluster's signal is a wave through the cloud: brain.h gives every point and
link its distance from each cluster, and the display lights what the wave's
front is passing.

The dots are paths, not circles: hexagons 3.2 px across, which at that size
read as round. A <circle r=...> with no cx, cy or transform passes the SDK's
checks and draws in a browser, but the OP-XY draws nothing for it; the circles
that do draw on the device carry a transform, cx="0" and cy="0". Paths draw
from this artwork without either, so the dots use them. A dot costs about 145 bytes either way, which with the 65,536-byte
limit on the artwork sets the counts.

The screen is the brain alone, over a black cover that hides the stock
sampler's page: the SDK's header (nodes 0..7), the links, the points. With no
sample loaded, a hint in the bottom left corner says how to load one (Grains'
and Ghost's words and key icon; a cover node hides it once there is one); with
one, holding Shift dims the brain and says how to swap it (sample_view.py,
shared with Grains, Ghost and Recur). The display turns the brain about its
vertical axis, every point every frame.
"""
import json
import pathlib
import re

import numpy as np

from sample_view import shift_cover, shift_overlay

ROOT = pathlib.Path(__file__).resolve().parent.parent
ID = "braindance"
FIRST_HEADER = 0                  # nodes 0..7: the header's bars
FIRST_LINK = FIRST_HEADER + 8
LINKS = 180                       # as many as the artwork's 65,536 bytes allow beside 270 dots
SCALE_Q7 = 128                    # model units to the table's integers
DOT_R = 1.6                       # the dots' radius, px, before the display's scale: they are the picture
LINK_W = 0.6                      # the links' width, px
LINK_REACH = 1.05                 # links authored a little longer than they are (the display only shortens them)
LINK_PX = (5.0, 26.0)             # a link's length on screen: near neighbours only, so it stays tidy
TILT = -14.0                      # degrees: seen a little from above (display.cpp's tilt)
TOP, BOTTOM, LEFT, RIGHT = 41, 219, 4, 476   # the brain's room below the header, px
SIZE_GUESS = 131                  # px a unit, to judge link lengths before the fit

# Palette: 0 black, 1..10 greys from dark to light, 11 dim red, 12 red,
# 13 dim blue, 14 blue, 15 white.
PALETTE = ["#000000", "#161616", "#262626", "#383838", "#4c4c4c", "#636363", "#7c7c7c", "#989898", "#b4b4b4",
           "#d0d0d0", "#e6e6e6", "#5a1414", "#ff2a2a", "#142a66", "#2f6bff", "#ffffff"]

# grey points: cortex surface, cerebellum, brainstem, a few deep inside
GREYS = (156, 36, 13, 21)
# kind, centre, points: 1 red (frontal), 2 blue (parietal), 3 red (deep), 4 blue (left hemisphere)
CLUSTERS = ((1, (0.3, 0.27, 0.24), 15), (2, (-0.27, 0.22, 0.25), 13),
            (3, (0.04, -0.03, 0.06), 8), (4, (0.32, 0.04, -0.27), 8))


def ellipsoid(p, c, r):
    return np.sqrt((((p - c) / r) ** 2).sum(axis=1)) - 1


def parts(p):
    """Each part's field, negative inside: cerebrum, cerebellum, brainstem."""
    folds = 0.045 * np.sin(23 * p[:, 0] + 3) * np.sin(19 * p[:, 1] + 1) * np.sin(17 * p[:, 2] + 2)
    lobes = []
    for side in (-1, 1):
        lobes.append(ellipsoid(p, np.array([0.0, 0.08, side * 0.2]), np.array([0.62, 0.40, 0.27])) + folds)
        lobes.append(ellipsoid(p, np.array([0.08, -0.16, side * 0.27]), np.array([0.34, 0.17, 0.15])) + folds)
    cerebrum = np.minimum.reduce(lobes)
    # the longitudinal fissure: a thin cut between the hemispheres, from the top
    cerebrum = cerebrum + np.where((np.abs(p[:, 2]) < 0.025) & (p[:, 1] > 0.1), 0.08, 0)
    cerebrum = cerebrum + np.where(p[:, 1] < -0.18, (-0.18 - p[:, 1]) * 2.2, 0)   # a flatter underside
    cerebellum = ellipsoid(p, np.array([-0.43, -0.27, 0.0]), np.array([0.21, 0.13, 0.33])) + 0.02 * np.sin(60 * p[:, 1])
    t = np.clip((p[:, 1] + 0.2) / -0.52, 0, 1)
    ax, az = -0.17 - 0.06 * t, 0 * t
    stem = np.sqrt((p[:, 0] - ax) ** 2 + (p[:, 2] - az) ** 2) / 0.075 - 1
    stem = np.where((p[:, 1] < -0.15) & (p[:, 1] > -0.74), stem, 5)
    return cerebrum, cerebellum, stem


def thin(points, n, rng):
    """Farthest-point sampling: n points spread evenly over the candidates."""
    n = min(n, len(points))
    chosen = [int(rng.integers(len(points)))]
    d = np.full(len(points), np.inf)
    for _ in range(n - 1):
        d = np.minimum(d, ((points - points[chosen[-1]]) ** 2).sum(axis=1))
        chosen.append(int(np.argmax(d)))
    return points[chosen]


def cloud(rng):
    """The points and their kinds: grey over the brain, then the four clusters."""
    box = rng.uniform([-0.72, -0.78, -0.55], [0.72, 0.55, 0.55], size=(1200000, 3))
    cerebrum, cerebellum, stem = parts(box)
    union = np.minimum.reduce([cerebrum, cerebellum, stem])
    near = np.abs(union) < 0.03
    groups = [(box[near & (cerebrum <= union)], GREYS[0]), (box[near & (cerebellum <= union)], GREYS[1]),
              (box[near & (stem <= union)], GREYS[2]),
              (box[(union < -0.25) & (np.abs(box[:, 1]) < 0.2) & (np.abs(box[:, 0]) < 0.35)], GREYS[3])]
    pts = [thin(g, n, rng) for g, n in groups]
    kinds = [np.zeros(len(x), dtype=int) for x in pts]
    for kind, centre, count in CLUSTERS:
        c = np.array(centre)
        source = box[union < -0.1] if kind == 3 else box[near]
        d = ((source - c) ** 2).sum(axis=1)
        pts.append(thin(source[np.argsort(d)[: count * 14]], count, rng))   # a tight clump, thinned
        kinds.append(np.full(count, kind))
    return np.vstack(pts), np.concatenate(kinds)


def links(q, rng):
    """LINKS pairs of near neighbours, shuffled: each point's four nearest are
    candidates if they are LINK_PX apart on screen at most, and a pair is taken
    while neither end has three links already."""
    p = q.astype(float) / SCALE_Q7
    d = np.sqrt(((p[:, None, :] - p[None, :, :]) ** 2).sum(axis=2)) * SIZE_GUESS
    np.fill_diagonal(d, np.inf)
    candidates = set()
    for i in range(len(p)):
        for j in np.argsort(d[i])[:4]:
            if LINK_PX[0] <= d[i, j] <= LINK_PX[1]:
                candidates.add((min(i, int(j)), max(i, int(j))))
    candidates = sorted(candidates)
    rng.shuffle(candidates)
    degree = np.zeros(len(p), dtype=int)
    out = []
    for a, b in candidates:
        if degree[a] < 3 and degree[b] < 3:
            out.append((a, b))
            degree[a] += 1
            degree[b] += 1
            if len(out) == LINKS:
                break
    assert len(out) == LINKS, f"only {len(out)} links: widen LINK_PX"
    return np.array(out)


def distances(p):
    """Each point's distance from each cluster's centre, in hundredths of a model unit (at most 255)."""
    out = np.zeros((len(p), 4), dtype=int)
    for kind, centre, _ in CLUSTERS:
        out[:, kind - 1] = np.clip(np.round(np.linalg.norm(p - np.array(centre), axis=1) * 100), 0, 255)
    return out


HEADER_TEXT_STYLE = []


def header_block():
    """The SDK's two-page header (from sdk.py new --controls 8), its bars as nodes 1..8."""
    greys = ["#000000", "#323232", "#4B4B4B", "#646464", "#7D7D7D", "#969696", "#AFAFAF", "#CDCDCD"]
    out = ['<!-- SDK two-page header: keep this block before machine artwork below y=38. -->',
           '<path d="M0,0 L480,0 L480,38 L0,38 Z" fill="#000000"/>']
    for i in range(8):
        out.append(f'<path id="{ID}_node_{FIRST_HEADER + i}" transform="matrix(1 0 0 1 {60 * i} 0)" '
                   f'd="M0,0 L60,0 L60,20 L0,20 Z" fill="{greys[i]}" stroke="{greys[i]}" stroke-width="0.5"/>')
    texts = [(0, 5, 10, "#CBCBCB", 10), (1, 125, 10, "#CBCBCB", 10), (2, 245, 10, "#000000", 10), (3, 365, 10, "#000000", 10),
             (4, 65, 17.4, "#CBCBCB", 20), (5, 185, 17.4, "#CBCBCB", 20), (6, 305, 17.4, "#000000", 20), (7, 425, 17.4, "#000000", 20),
             (8, 5, 33, "#5A5A5A", 10), (9, 125, 33, "#5A5A5A", 10), (10, 245, 33, "#5A5A5A", 10), (11, 365, 33, "#5A5A5A", 10),
             (12, 65, 33, "#707070", 13), (13, 185, 33, "#707070", 13), (14, 305, 33, "#707070", 13), (15, 425, 33, "#707070", 13)]
    HEADER_TEXT_STYLE[:] = [(slot, fill, size) for slot, x, y, fill, size in texts]
    for slot, x, y, fill, size in texts:
        out.append(f'<text id="{ID}_text_{slot}" transform="matrix(1 0 0 1 {x} {y})" fill="{fill}" '
                   f'font-family="UniversNextPro-Light" font-size="{size}px">-</text>')
    return out


def fmt(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s




def fit(q):
    """The brain's place and size on screen: the most it can fill below the
    header at every turn, as the display projects it (Q7 points)."""
    x, y, z = (q[:, k].astype(float) / SCALE_Q7 for k in range(3))
    t = np.radians(TILT)
    top, bottom, side = -9.0, 9.0, 0.0
    for a in np.radians(np.arange(0, 360, 2.0)):
        rx = x * np.cos(a) + z * np.sin(a)
        rz = z * np.cos(a) - x * np.sin(a)
        sy = y * np.cos(t) - rz * np.sin(t)
        top, bottom, side = max(top, sy.max()), min(bottom, sy.min()), max(side, np.abs(rx).max())
    margin = DOT_R * 1.6 + 1                      # the largest dot, and a pixel
    size = int(min((BOTTOM - TOP - 2 * margin) / (top - bottom), ((RIGHT - LEFT) / 2 - margin) / side))
    centre_y = int(round(TOP + margin + top * size))
    return 240, centre_y, size


def check(svg_text):
    """The artwork's limits: 65,536 bytes (kept under 65,000), one subpath of at most
    64 points a path, and no circle but the palette's (which carry cx and cy)."""
    size = len(svg_text.encode())
    assert size <= 65000, f"panel.svg.fragment is {size} bytes: the limit is 65,536"
    for d in re.findall(r' d="([^"]*)"', svg_text):
        assert d.count("M") == 1 and d.count("L") + 1 <= 64, d[:60]
    for circle in re.findall(r"<circle [^>]*>", svg_text):
        assert ' cx="' in circle and ' cy="' in circle and "_palette_" in circle, \
            "a circle node without cx, cy and a transform draws nothing on the OP-XY; use a path: " + circle[:60]
    return size


def hint(cover):
    """With no sample: "load a sample", "shift +" and the Sample key's icon, as
    Grains and Ghost word and draw it, on one line in the bottom left corner,
    where the brain never reaches (it stays within about 95 px of the middle,
    and only its stem comes this low). Static artwork; the cover node hides it
    once a sample is assigned. The widths are the face's advances, widened 15%
    as the OP-XY draws artwork text (Ghost's tools/make_panel.py): "load a
    sample" 108.3 px at 15 px, "shift +" 43.2 at 13."""
    font = 'font-family="UniversNextPro-Light"'
    base, icon, gap, x0 = 213.0, 15.0, 10.0, 8.0
    shift_x = x0 + 108.3 + gap
    icon_x = shift_x + 43.2 + gap
    out = [f'<text transform="matrix(1 0 0 1 {fmt(x0)} {fmt(base)})" fill="#CBCBCB" {font} font-size="15px">load a sample</text>',
           f'<text transform="matrix(1 0 0 1 {fmt(shift_x)} {fmt(base)})" fill="#7D7D7D" {font} font-size="13px">shift +</text>']
    k, kx, ky = icon / 28, icon_x, base - 12.5
    at = lambda x, y: (kx + (x - 256) * k, ky + (y - 129) * k)
    def path(points, stroke, width, fill="none"):
        d = "M" + " L".join(f"{fmt(x)},{fmt(y)}" for x, y in points) + " Z"
        return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
    out.append(path([at(x, y) for x, y in ((257.4237, 129), (282.5763, 129), (284, 130.4237), (284, 155.5763), (282.5763, 157),
                                           (257.4237, 157), (256, 155.5763), (256, 130.4237))], "#7D7D7D", 0.8))
    out.append(path([at(270 + 9.0763 * np.cos(2 * np.pi * i / 20), 142.8814 + 9.0763 * np.sin(2 * np.pi * i / 20)) for i in range(20)], "#7D7D7D", 0.6))
    for x, top, bottom in ((265.6254, 142.2881, 143.5932), (267.0492, 142.2881, 143.5932), (268.5322, 139.2627, 146.5593),
                           (269.9559, 140.8644, 145.0763), (271.439, 142.2881, 143.5932), (272.922, 142.2881, 143.5932),
                           (274.3458, 142.2881, 143.5932)):
        out.append(path([at(x, top), at(x + 0.8, top), at(x + 0.8, bottom), at(x, bottom)], "#7D7D7D", 0.5, "#7D7D7D"))
    x1 = icon_x + icon + 4
    out.append(f'<path id="{ID}_node_{cover}" d="M{fmt(x0 - 4)},198 L{fmt(x1)},198 L{fmt(x1)},220 L{fmt(x0 - 4)},220 Z" '
               f'fill="#000000" stroke="#000000" stroke-width="0.5"/>')
    return out


def table(name, kind, values):
    return f"static const opxy::{kind} {name}[{len(values)}] = {{" + ", ".join(str(int(v)) for v in values) + "};"


def main():
    rng = np.random.default_rng(7)
    pts, kind = cloud(rng)
    # the draw order: the clusters last, so they sit on top
    order = np.argsort(kind, kind="stable")
    pts, kind = pts[order], kind[order]
    q = np.clip(np.round(pts * SCALE_Q7), -127, 127).astype(int)
    twinkle = rng.integers(0, 256, size=len(q))
    centre_x, centre_y, size_px = fit(q)
    pairs = links(q, rng)
    qf = q.astype(float) / SCALE_Q7
    length = np.linalg.norm(qf[pairs[:, 0]] - qf[pairs[:, 1]], axis=1) * size_px * LINK_REACH
    length_q2 = np.maximum(4, 2 * np.round(length * 2)).astype(int)   # to the half pixel: shorter to write
    point_dist = distances(qf)
    link_dist = distances((qf[pairs[:, 0]] + qf[pairs[:, 1]]) / 2)
    first_point = FIRST_LINK + len(pairs)
    # after the brain: the hint's cover, then the Shift overlay's cover and its key icon (3 nodes)
    hint_cover = first_point + len(q)
    overlay_cover = hint_cover + 1
    overlay_icon = overlay_cover + 1
    nodes = overlay_icon + 3
    assert nodes <= 512 and len(q) <= 65535, nodes

    def rows(name, kind_, values):
        return f"static const opxy::{kind_} {name}[{len(values)}][4] = {{" + ", ".join("{" + ", ".join(str(int(v)) for v in r) + "}" for r in values) + "};"

    lines = ["// brain.h: written by tools/make_panel.py. The brain as a cloud of points and the links between them.",
             "// Points (Q7 model units: x front, y up, z right): each one's kind, a twinkle seed and its distance",
             "// from each cluster (hundredths of a unit). Links, in the order they appear as the playing gets",
             "// denser: their ends, authored length (Q2 px) and their middle's distance from each cluster.",
             "#pragma once", "#include <opxy/types.hpp>", "", "namespace braindance { namespace brain {",
             f"const opxy::u32 points = {len(q)}, links = {len(pairs)};",
             f"const opxy::u32 first_header = {FIRST_HEADER}, first_link = {FIRST_LINK}, first_point = {first_point}, nodes = {nodes};",
             f"const opxy::u32 hint_cover = {hint_cover}, overlay_cover = {overlay_cover}, overlay_icon = {overlay_icon}, overlay_text = 16;",
             f"const opxy::i32 centre_x = {centre_x}, centre_y = {centre_y};   // the brain's middle, px",
             f"const opxy::i32 size_px = {size_px};                    // px per model unit: it fills the screen below the header",
             "// palette: 0 black, 1..10 greys dark to light, 11 dim red, 12 red, 13 dim blue, 14 blue, 15 white",
             "const opxy::u32 dim_red = 11, red = 12, dim_blue = 13, blue = 14, white = 15;",
             table("x", "i8", q[:, 0]), table("y", "i8", q[:, 1]), table("z", "i8", q[:, 2]),
             table("kind", "u8", kind), table("twinkle", "u8", twinkle), rows("point_far", "u8", point_dist),
             table("link_a", "u16", pairs[:, 0]), table("link_b", "u16", pairs[:, 1]),
             table("link_length", "u16", length_q2), rows("link_far", "u8", link_dist),
             f"// points of each kind: {[int((kind == k).sum()) for k in range(5)]}",
             "}}"]
    (ROOT / "brain.h").write_text("\n".join(lines) + "\n")

    # A black cover over the whole screen first: without it the stock sampler's page shows behind the brain.
    svg = [f'<g id="{ID}_panel">', '<path d="M0,0 L480,0 L480,220 L0,220 Z" fill="#000000"/>'] + header_block()
    # The links first, under the points: thin filled ribbons (no stroke: it would only cost bytes) from
    # the origin along +x, turned and shortened by the display (paths draw on the OP-XY without a
    # transform). Then the points: hexagon paths, not circles (the docstring says why).
    # a dot: a hexagon about the origin, begun at a corner so it closes over a whole side
    dot = "M" + " L".join(f"{fmt(round(DOT_R * np.cos(np.pi * k / 3), 1))},{fmt(round(DOT_R * np.sin(np.pi * k / 3), 1))}" for k in range(6)) + " Z"
    h = LINK_W / 2
    for e, lq in enumerate(length_q2):
        L = fmt(lq / 4)
        svg.append(f'<path id="{ID}_node_{FIRST_LINK + e}" d="M0,-{h} L{L},-{h} L{L},{h} L0,{h} Z" fill="#4c4c4c"/>')
    for i in range(len(q)):
        svg.append(f'<path id="{ID}_node_{first_point + i}" d="{dot}" fill="#b4b4b4" stroke="#b4b4b4" stroke-width="0.3"/>')
    svg += hint(hint_cover)
    svg.append(shift_cover(ID, overlay_cover))
    svg += shift_overlay(ID, overlay_icon, 16)
    for i, colour in enumerate(PALETTE):
        svg.append(f'<circle id="{ID}_palette_{i}" cx="640" cy="0" r="1" fill="{colour}" stroke="{colour}" stroke-width="0.3"/>')
    svg.append("</g>")
    text = "\n".join(svg) + "\n"
    size = check(text)
    (ROOT / "panel.svg.fragment").write_text(text)
    px = length / LINK_REACH
    print(f"{len(q)} points, {len(pairs)} links ({px.min():.0f} to {px.max():.0f} px, median {np.median(px):.0f}), {nodes} nodes, "
          f"{size_px} px a unit at ({centre_x}, {centre_y}), artwork {size / 1024:.1f} KiB")
    (ROOT / "docs" / "brain-points.json").write_text(json.dumps(
        {"points": q.tolist(), "kinds": kind.tolist(), "links": pairs.tolist()}))
    (ROOT / "web" / "panel.json").write_text(json.dumps({
        "palette": PALETTE, "first": {"header": FIRST_HEADER, "link": FIRST_LINK, "point": first_point},
        "radius": DOT_R, "width": LINK_W, "links": (length_q2 / 4).tolist(), "points": len(q), "hint": [8.0, 213.0],
        "texts": [[slot, fill, size] for slot, fill, size in HEADER_TEXT_STYLE]}))


if __name__ == "__main__":
    main()
