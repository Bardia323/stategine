"""The agent's lens: look at a place while editing it, in one run.

Any program that draws its world through sg::render::GLWorldView, run with
SG_LENS, looks at a point or a thing of the room it stands in from nine eyes
(every side, every scale), holds one eye still and measures what changes with
nothing moving, says where in each picture the world's faults are, and ends
(src/gl/WorldLensSheet.cpp). This runs it on the GPU and lays one sheet:

    python3 tools/sg_lens.py --out DIR (--at x,y,z | --thing ID) [--yaw DEG] [--r M]
        [--hold S] [--warm FRAMES] [--env K=V ...] -- <program> <args...>

    e.g. (UNLIT's parlour, its sconce):
    sg_lens.py --out ~/stategine/reports/guardrails/lens --thing lamp_end_sconce --bright 6 \
        --env UNLIT_HALL=2 -- ~/stategine/memory-leak-pt/build/unlit 20 none.png 960 540

--bright N lifts the exposure N times while the lens looks (a dark room seen);
--clean takes the film's grain, readout noise and dither off. Flicker is
measured on 8 by 8 blocks against each block's median over the still frames,
so grain (a pixel wide) is not flicker; `grain` is reported beside it.

DIR gets sheet.png (the nine, the still eye's first and last frames, the
faults numbered where they are), lens.json (eyes, the still eye's light per
frame, faults) and every picture. The verdict is printed: black frames,
flicker and exposure turns with a still camera are faults. Run before and
after every placement. The GPU goes through reports/pt/tools/gpu.sh when it
is there (one job at a time), else sg-gpu.
"""
import argparse
import json
import os
import subprocess
import sys
import time


def main():
    if "--" not in sys.argv:
        print(__doc__)
        return 2
    cut = sys.argv.index("--")
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--at")
    ap.add_argument("--thing")
    ap.add_argument("--yaw", type=float)
    ap.add_argument("--r", type=float)
    ap.add_argument("--hold", type=float, default=2.0)
    ap.add_argument("--warm", type=int, default=40)
    ap.add_argument("--settle", type=int, default=8)
    ap.add_argument("--bright", type=float, default=1.0, help="the picture's exposure times this while the lens looks (a dark room: 4-16)")
    ap.add_argument("--clean", action="store_true", help="no grain, readout noise or dither while the lens looks")
    ap.add_argument("--env", action="append", default=[])
    a = ap.parse_args(sys.argv[1:cut])
    program = sys.argv[cut + 1:]
    # (The program found where it was named from, whatever folder it then runs in.)
    if program and os.sep in program[0] or (program and os.path.exists(program[0])):
        program[0] = os.path.abspath(program[0])
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    spec = [f"out={out}", f"hold={a.hold}", f"warm={a.warm}", f"settle={a.settle}"]
    if a.at:
        spec.append(f"at={a.at}")
    if a.thing:
        spec.append(f"thing={a.thing}")
    if a.yaw is not None:
        spec.append(f"yaw={a.yaw}")
    if a.bright != 1.0:
        spec.append(f"bright={a.bright}")
    if a.clean:
        spec.append("clean=1")
    if a.r is not None:
        spec.append(f"r={a.r}")
    env = dict(os.environ, SG_LENS=";".join(spec), SG_WINDOW_MONITOR="other")
    for kv in a.env:
        k, v = kv.split("=", 1)
        env[k] = v
    gpu = os.path.expanduser("~/stategine/reports/pt/tools/gpu.sh")
    runner = [gpu] if os.path.exists(gpu) else ["sg-gpu"]
    t0 = time.time()
    try:
        os.remove(os.path.join(out, "lens.json"))
    except OSError:
        pass
    r = subprocess.run(runner + program, env=env, cwd=os.path.dirname(os.path.abspath(program[0])) or ".", capture_output=True, text=True)
    took = time.time() - t0
    path = os.path.join(out, "lens.json")
    if not os.path.exists(path):
        sys.stderr.write(r.stdout[-3000:] + r.stderr[-3000:])
        print(f"lens: the program ended before the lens was done ({took:.1f} s) - give it more seconds of frames")
        return 1
    with open(path) as f:
        lens = json.load(f)
    sheet(out, lens)
    h = lens["hold"]
    blank = [f for f in lens.get("feeds", []) if f["blank"]]
    pops = h.get("pop_frames", [])
    bad = h["black_frames"] > 0 or h["flicker_frames"] > 0 or h["exposure_turns"] > 2 or bool(blank) or bool(pops)
    print(f"lens: {out}/sheet.png in {took:.1f} s - {lens['room']}, {len(lens['faults'])} faults said, "
          f"still eye {h['frames']} frames: {h['black_frames']} black, {h['flicker_frames']} flickering (a pop and back), {len(pops)} pops (a change that stays{': frames ' + ','.join(map(str, pops)) if pops else ''}), {h['exposure_turns']} exposure turns, grain {h.get('grain', 0):.3f}"
          + f"; {len(blank)} of {len(lens.get('feeds', []))} screens blank"
          + (" - FAULT" if bad else " - steady"))
    if os.path.exists(os.path.join(out, "hold_worst.png")):
        print(f"  the still frame that changed most: {h.get('worst_frame')} ({100 * h.get('worst_blocks', 0):.1f}% of blocks) - hold_worst_before.png / hold_worst.png")
    for f in blank:
        print(f"  screen {f['portal']} ({f['world']}) came out one flat colour (mean {f['mean']:.3f}): blank")
    for i, f in enumerate(lens["faults"]):
        seen = [v["name"] for v in lens["views"] for m in v["marks"] if m["fault"] == i]
        print(f"  [{i}] {f}" + (f"  (in view: {', '.join(sorted(set(seen)))})" if seen else ""))
    return 1 if bad else 0


def sheet(out, lens):
    sys.path.insert(0, "")
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("lens: no PIL here - the pictures are in", out, "(use ~/comfyui/venv2/bin/python for the sheet)")
        return
    tiles = [(v["name"], os.path.join(out, v["file"]), v["marks"]) for v in lens["views"]]
    tiles += [("still: first", os.path.join(out, "hold_first.png"), []), ("still: last", os.path.join(out, "hold_last.png"), [])]
    if os.path.exists(os.path.join(out, "hold_worst.png")):
        wf = lens["hold"].get("worst_frame", -1)
        tiles += [(f"still: before frame {wf}", os.path.join(out, "hold_worst_before.png"), []), (f"still: frame {wf}, the most changed", os.path.join(out, "hold_worst.png"), [])]
    w, hh = lens["size"]
    tw = 480
    th = int(tw * hh / max(1, w))
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    graph_h = 90
    img = Image.new("RGB", (cols * tw, rows * (th + 18) + graph_h + 10), (20, 20, 20))
    d = ImageDraw.Draw(img)
    for i, (name, p, marks) in enumerate(tiles):
        x, y = (i % cols) * tw, (i // cols) * (th + 18)
        if os.path.exists(p):
            t = Image.open(p).convert("RGB").resize((tw, th))
            img.paste(t, (x, y + 18))
        d.text((x + 4, y + 3), name, fill=(230, 230, 230))
        s = tw / max(1, w)
        for m in marks:
            px, py = m["px"][0] * s + x, m["px"][1] * s + y + 18
            d.ellipse((px - 7, py - 7, px + 7, py + 7), outline=(255, 60, 40), width=2)
            d.text((px + 9, py - 6), str(m["fault"]), fill=(255, 90, 60))
    # The still eye's light, frame by frame (red where a frame flickered).
    light = lens["hold"]["light"]
    changed = lens["hold"]["changed"]
    gy = rows * (th + 18) + 5
    d.text((4, gy), "still eye: mean light per frame (red: a frame whose 8x8 blocks pop and come back - grain is not counted; yellow: a pop that stays)", fill=(200, 200, 200))
    if light:
        lo, hi = min(light), max(light)
        span = max(hi - lo, 1e-4)
        n = len(light)
        pts = [(4 + i * (cols * tw - 8) / max(1, n - 1), gy + graph_h - 4 - (v - lo) / span * (graph_h - 20)) for i, v in enumerate(light)]
        d.line(pts, fill=(120, 200, 255), width=2)
        for i, c in enumerate(changed):
            if c > 0.003 and i < len(pts):
                d.ellipse((pts[i][0] - 2, pts[i][1] - 2, pts[i][0] + 2, pts[i][1] + 2), fill=(255, 50, 50))
        for i in lens["hold"].get("pop_frames", []):
            if i < len(pts):
                d.ellipse((pts[i][0] - 4, pts[i][1] - 4, pts[i][0] + 4, pts[i][1] + 4), outline=(255, 220, 40), width=2)
        d.text((cols * tw - 260, gy), f"light {lo:.3f}..{hi:.3f}", fill=(200, 200, 200))
    img.save(os.path.join(out, "sheet.png"))


if __name__ == "__main__":
    sys.exit(main())
