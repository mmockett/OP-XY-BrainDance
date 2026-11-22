// Brain Dance's screens as the OP-XY draws them, without compiling for the host:
// runs display.cpp in the web mockup's WebAssembly (web/build.sh), applies each
// frame to panel.svg.fragment (node transforms, palette paint, header text) and
// writes SVG screens, a contact sheet of every control at 0, 50 and 100, and
// the redraw count (nodes changed a frame at 60 Hz).
//
//   XY_FONT=<UniversNextPro-Light.ttf> node tools/screens.mjs [machine folder] [out folder]
//
// The font is stock content: point XY_FONT at your copy, never commit it. Each
// .html in the out folder renders at 1:1; capture them with headless Chrome
// (--allow-file-access-from-files --virtual-time-budget=2000 --screenshot).
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(process.argv[2] || path.join(path.dirname(new URL(import.meta.url).pathname), ".."));
const out = path.resolve(process.argv[3] || "screens");
const font = process.env.XY_FONT;
if (!font) { console.error("Set XY_FONT to UniversNextPro-Light.ttf"); process.exit(1); }
fs.mkdirSync(out, { recursive: true });

const ID = "braindance", U = 1024, HIDDEN_X = 640;
const panel = fs.readFileSync(path.join(root, "panel.svg.fragment"), "utf8").split("\n").filter(l => l.trim());
const wasm = new WebAssembly.Module(fs.readFileSync(path.join(root, "web", "braindance.wasm")));
const palette = {};
for (const l of panel) {
  const m = l.match(new RegExp(`id="${ID}_palette_(\\d+)".*?fill="([^"]+)" stroke="([^"]+)" stroke-width="([^"]+)"`));
  if (m) palette[+m[1]] = { fill: m[2], stroke: m[3], width: m[4] };
}
const DEFAULTS = [50.5 / 60, 0.7, 0.4, 0.3, 0.4, 0.7, 0.7, 0.55];   // the default preset, Shred
const NAMES = ["algo", "dense", "chaos", "glitch", "tone", "color", "ring", "crush"];

// A fresh screen: controls 0..1, then frames of keys held (or released).
function session(values, page = 0) {
  const ex = new WebAssembly.Instance(wasm, {}).exports;
  const params = () => new Int32Array(ex.memory.buffer, ex.bd_params(), 8);
  values.forEach((v, i) => { params()[i] = Math.min(0x7fffffff, Math.round(v * 0x7fffffff)); });
  ex.bd_page(page);
  let serial = 0, held = 0, last = null;
  return {
    run(frames, keys, reduced = 0) {
      if (keys > held) serial++;
      held = keys;
      const churn = [];
      for (let k = 0; k < frames; k++) {
        ex.bd_display(17, held, serial, reduced);
        const now = this.nodes();
        if (last) { let n = 0; for (let i = 0; i < now.length; i += 7) for (let j = 0; j < 7; j++) if (now[i + j] !== last[i + j]) { n++; break; } churn.push(n); }
        last = now;
      }
      return churn;
    },
    sample(frames) { ex.bd_overview(frames, 44100); return this; },   // the track's sample, as the screen sees it
    shift(held) { ex.bd_shift(held ? 1 : 0); return this; },
    nodes() { return Int32Array.from(new Int32Array(ex.memory.buffer, ex.bd_frame() + ex.bd_nodes_offset(), ex.bd_frame_nodes() * 7)); },
    svg() { return draw(ex); },
  };
}

function fmt(v) { return (+v.toFixed(4)).toString(); }

// One frame of the device's drawing, as SVG.
function draw(ex) {
  const buf = ex.memory.buffer, base = ex.bd_frame();
  const nodes = new Int32Array(buf, base + ex.bd_nodes_offset(), ex.bd_frame_nodes() * 7);
  const tb = ex.bd_text_bytes(), texts = base + ex.bd_texts_offset();
  const body = [];
  for (const l of panel) {
    if (l.startsWith("<g") || l.startsWith("</g") || l.startsWith("<!--")) continue;
    const pal = l.match(new RegExp(`id="${ID}_palette_`));
    if (pal) continue;
    const nm = l.match(new RegExp(`id="${ID}_node_(\\d+)"`));
    const tm = l.match(new RegExp(`id="${ID}_text_(\\d+)"`));
    if (nm) {
      const i = +nm[1], [x, y, s, c, sn, shade, paint] = nodes.slice(7 * i, 7 * i + 7);
      if (x >= HIDDEN_X * U) continue;
      const k = s / U, a = k * c / U, b = k * sn / U;
      let el = l.replace(/ transform="[^"]*"/, "").replace(/^<(\w+)/, `<$1 transform="matrix(${fmt(a)} ${fmt(b)} ${fmt(-b)} ${fmt(a)} ${fmt(x / U)} ${fmt(y / U)})"`);
      if (paint && palette[shade]) {
        const p = palette[shade];
        el = el.replace(/ stroke="[^"]*"/, ` stroke="${p.stroke}"`).replace(/ stroke-width="[^"]*"/, ` stroke-width="${p.width}"`);
        if (paint === 2 && !/fill="none"/.test(el)) el = el.replace(/ fill="[^"]*"/, ` fill="${p.fill}"`);
      }
      if (i < (ex.bd_paths_offset ? 1 : 0) && / d="/.test(el) && /fill="none"/.test(el)) {
        // a reshaping outline (the old ring): its points from the frame
        const pts = new Int32Array(buf, base + ex.bd_paths_offset() + 4, 128);
        let d = "";
        for (let q = 0; q < 64; q++) d += (q ? " L" : "M") + fmt(pts[2 * q] / U) + "," + fmt(pts[2 * q + 1] / U);
        el = el.replace(/ d="[^"]*"/, ` d="${d} Z"`);
      }
      body.push(el);
    } else if (tm) {
      const at = texts + +tm[1] * tb, t = new Int32Array(buf, at, 3), bytes = new Uint8Array(buf, at + 12, 24);
      let str = ""; for (const ch of bytes) { if (!ch) break; str += String.fromCharCode(ch); }
      if (!str) continue;
      const esc = str.replace(/&/g, "&amp;").replace(/</g, "&lt;");
      body.push(l.replace(/ transform="[^"]*"/, ` transform="matrix(${fmt(t[2] / U)} 0 0 ${fmt(t[2] / U)} ${fmt(t[0] / U)} ${fmt(t[1] / U)})"`).replace(/>-</, `>${esc}<`));
    } else body.push(l);
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="480" height="222" viewBox="0 0 480 222">` +
    `<rect width="480" height="222" fill="#000"/>${body.join("")}</svg>`;
}

function page(cells, cols, scale, title) {
  const css = `@font-face{font-family:"UniversNextPro-Light";src:url("file://${font}")}` +
    `body{margin:0;background:#111;color:#bbb;font:12px "UniversNextPro-Light"}` +
    `.g{display:grid;grid-template-columns:repeat(${cols},${480 * scale}px);gap:${14 * scale}px ${10 * scale}px;padding:${14 * scale}px}` +
    `.c div{height:${16 * scale}px;font-size:${11 * scale}px}.c svg{width:${480 * scale}px;height:${222 * scale}px;display:block}` +
    `h1{font-weight:normal;font-size:${14 * scale}px;margin:${14 * scale}px ${14 * scale}px 0}`;
  const width = cols * 480 * scale + (cols - 1) * 10 * scale + 28 * scale;
  const rows = Math.ceil(cells.length / cols);
  const height = rows * (222 + 16) * scale + (rows - 1) * 14 * scale + 28 * scale + (title ? 44 * scale : 0) + 8 * scale;
  return { html: `<!doctype html><meta charset="utf-8"><style>${css}</style>${title ? `<h1>${title}</h1>` : ""}<div class="g">` +
    cells.map(([label, svg]) => `<div class="c"><div>${label}</div>${svg}</div>`).join("") + "</div>", width, height };
}

const sizes = {};
function write(name, { html, width, height }) {
  fs.writeFileSync(path.join(out, name + ".html"), html);
  sizes[name] = [width, height];
}

// --anim: an animation instead, as strips of frames at twice the size (anim-N.html, 36 frames to a
// strip, the most a headless Chrome screenshot holds): 12.8 s at 30 frames a second of the default
// preset, a key added every three seconds, the display stepped at 60 Hz. Capture each strip and split
// it with ffmpeg's untile filter.
if (process.argv.includes("--anim")) {
  const s = session(DEFAULTS).sample(441000);
  const frames = [];
  for (let f = 0; f < 384; f++) { s.run(2, 1 + Math.min(3, Math.floor(f / 90))); frames.push(s.svg()); }
  const per = 36;
  for (let n = 0; n * per < frames.length; n++) {
    const part = frames.slice(n * per, (n + 1) * per);
    const css = `@font-face{font-family:"UniversNextPro-Light";src:url("file://${font}")}body{margin:0;background:#000}svg{width:960px;height:444px;display:block}`;
    fs.writeFileSync(path.join(out, `anim-${String(n).padStart(2, "0")}.html`), `<!doctype html><meta charset="utf-8"><style>${css}</style>${part.join("")}`);
    sizes[`anim-${String(n).padStart(2, "0")}`] = [960, 444 * part.length];
  }
  fs.writeFileSync(path.join(out, "sizes.json"), JSON.stringify(sizes));
  console.log(`${frames.length} frames in ${Math.ceil(frames.length / per)} strips`);
  process.exit(0);
}

// Single screens.
const shots = [];
const at = (i, v) => { const c = DEFAULTS.slice(); c[i] = v; return c; };
{ const s = session(at(1, 0)).sample(441000); s.run(30, 0); shots.push(["sparse: dense 0, no keys", s.svg()]); }
{ const s = session(DEFAULTS).sample(441000); s.run(100, 1); shots.push(["the default, one key", s.svg()]); }
{ const s = session(at(1, 1)).sample(441000); s.run(100, 4); shots.push(["dense: dense 100, four keys", s.svg()]); }
{ const s = session(at(6, 1)).sample(441000); s.run(131, 2); shots.push(["ring 100: a wave crossing", s.svg()]); }
{ const s = session(DEFAULTS); s.run(30, 0); shots.push(["rest, no sample: the hint", s.svg()]); }
{ const s = session(DEFAULTS).sample(441000); s.run(120, 2); s.shift(1); s.run(2, 2); shots.push(["Shift held with a sample", s.svg()]); }
{ const s = session(at(7, 1)).sample(441000); s.run(100, 2); shots.push(["crush 100: the far side stripped", s.svg()]); }
{ const s = session([59.5 / 60, 1, 1, 1, 1, 0.3, 1, 0.5], 1).sample(441000); s.run(173, 8); shots.push(["shrd9, every control up, page 2", s.svg()]); }
for (const [i, [label, svg]] of shots.entries()) {
  fs.writeFileSync(path.join(out, `screen-${i}.svg`), svg);
  write(`screen-${i}`, page([[label, svg]], 1, 1, ""));
  write(`screen-${i}@2x`, page([[label, svg]], 1, 2, ""));
}
write("screens", page(shots, 2, 1, "Brain Dance"));
write("screens@2x", page(shots, 2, 2, "Brain Dance"));

// Contact sheet: every control at 0, 50 and 100, two keys held, 1.5 s in.
const cells = [];
for (let c = 0; c < 8; c++)
  for (const v of [0, 0.5, 1]) {
    const values = DEFAULTS.slice();
    values[c] = v;
    const s = session(values, c >= 4 ? 1 : 0);
    s.run(90, 2);
    cells.push([`${NAMES[c]} ${Math.round(v * 100)}`, s.svg()]);
  }
write("controls-1x", page(cells, 3, 1, "Every control at 0, 50 and 100, two keys held"));
write("controls", page(cells, 3, 2, "Every control at 0, 50 and 100, two keys held"));

// The redraw count.
const report = [];
const churnOf = (label, values, keys) => {
  const s = session(values);
  s.run(60, keys);
  const c = s.run(600, keys);
  const avg = c.reduce((a, b) => a + b, 0) / c.length;
  report.push(`${label.padEnd(32)} ${String(keys).padStart(2)} keys: ${avg.toFixed(1).padStart(5)} a frame, peak ${Math.max(...c)}`);
};
{ const s = session(DEFAULTS); s.run(60, 0); const c = s.run(300, 0); report.push(`${"rest".padEnd(32)}  0 keys: ${(c.reduce((a, b) => a + b, 0) / c.length).toFixed(1).padStart(5)} a frame, peak ${Math.max(...c)}`); }
churnOf("defaults", DEFAULTS, 1);
churnOf("defaults", DEFAULTS, 4);
["lanes", "break", "data", "euclid", "pulse", "shred"].forEach((n, a) => churnOf(`${n}, every control up`, [(a + 0.5) / 6, 1, 1, 1, 1, 1, 1, 0], 8));
fs.writeFileSync(path.join(out, "churn.txt"), report.join("\n") + "\n");
fs.writeFileSync(path.join(out, "sizes.json"), JSON.stringify(sizes));
console.log(report.join("\n"));
