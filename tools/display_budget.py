"""The display's declared budget: the frame's shape from brain.h, and
DisplayState's size from the compiled type (host C++)."""
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
import os
SDK = pathlib.Path(os.environ.get("OPEN_XY", ROOT.parent / "open-xy")) / "sdk" / "include"   # the Open-XY checkout beside this repo


def budget():
    nodes = int(re.search(r"nodes = (\d+);", (ROOT / "brain.h").read_text()).group(1))
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "size.cpp"
        src.write_text('#include "display.h"\n#include <cstdio>\nint main(){std::printf("%u %u", unsigned(sizeof(braindance::DisplayState)), unsigned(alignof(braindance::DisplayState)));}\n')
        exe = pathlib.Path(tmp) / "size"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++11", f"-I{SDK}", f"-I{ROOT}", str(src), "-o", str(exe)], check=True)
        size, align = map(int, subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.split())
    return {"stateBytes": size, "stateAlignment": align, "paths": 0, "points": 0, "nodes": nodes, "texts": 18, "palette": 16}


if __name__ == "__main__":
    print(budget())
