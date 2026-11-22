"""Builds the web mockup, web/index.html, from index.src.html: the engine's
WebAssembly (web/build.sh), the screen's artwork (tools/make_panel.py) and the
presets (tools/make_presets.py), all inlined. python web/make_page.py"""
import base64
import json
import pathlib
import sys

WEB = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(WEB.parent / "tools"))
import make_presets  # noqa: E402


def main():
    wasm = base64.b64encode((WEB / "braindance.wasm").read_bytes()).decode()
    panel = (WEB / "panel.json").read_text()
    presets = [{"name": p[0], "algo": p[1], "role": p[9],
                "values": [make_presets.algo_value(p[1])] + list(p[2:9])} for p in make_presets.PRESETS]
    page = (WEB / "index.src.html").read_text()
    page = page.replace("/*__WASM__*/", wasm).replace("/*__PANEL__*/null", panel)
    page = page.replace("/*__PRESETS__*/null", json.dumps(presets))
    (WEB / "index.html").write_text(page)
    print(f"web/index.html: {len(page) // 1024} KiB")


if __name__ == "__main__":
    main()
