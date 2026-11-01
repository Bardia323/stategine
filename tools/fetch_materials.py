"""Fetch photographed materials (CC0: Poly Haven and ambientCG) for a project,
from a table of its own, and lay each in <out>/<name>/ under one set of names:

    albedo.jpg      colour, sRGB                       -> texture.layer
    normal_gl.png   normal map, OpenGL's (green up)    -> texture.normal_layer
    arm.png         red AO, green roughness, blue metal (linear) -> texture.surface_layer
    roughness.png, ao.png, height.png, metal.png, opacity.png   the separate maps, kept for tools

The table (JSON) names each material:

    {"wall_tiles": {"site": "acg", "asset": "Tiles133A", "res": "2K",
                    "recolour": null, "saturation": 1.0, "contrast": 1.0,
                    "gain": 1.0, "target": [0.7, 0.68, 0.62], "rough": null,
                    "metal": false}, ...}

`recolour` (sRGB hex) replaces the hue, the scan's own light and dark kept;
else `saturation` lowers it about each pixel's own grey. `gain` is a linear
multiplier (one or per channel) to bring the scan to a real albedo
(docs/materials.md); `target` instead brings its mean to a linear albedo.
`rough` moves the roughness's mean, its spread kept. Only a `metal` material
keeps a metal channel: in a dielectric's scan it is noise.

Downloads are data, never run: they land in <out>/_dl/, and from a zip only
the named image members are read (never extracted by its paths).

    python -I tools/fetch_materials.py <table.json> <out dir> [name ...]
"""
import io
import json
import os
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image

UA = {"User-Agent": "stategine-material-fetch/1.0"}


def get(url, path):
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=180) as r:
        data = r.read()
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)
    return path


def open_image(data):
    im = Image.open(io.BytesIO(data))
    im.load()
    return im


def polyhaven(asset, res, dl):
    files = json.loads(urllib.request.urlopen(urllib.request.Request(
        f"https://api.polyhaven.com/files/{asset}", headers=UA), timeout=60).read())
    want = {"albedo": ["Diffuse", "diff", "coll1"], "normal": ["nor_gl"], "arm": ["arm"],
            "roughness": ["Rough", "rough"], "ao": ["AO", "ao"], "height": ["Displacement", "disp"],
            "metal": ["Metal", "metal"]}
    maps = {}
    for key, names in want.items():
        for n in names:
            if n in files and res in files[n]:
                fmt = "jpg" if key == "albedo" and "jpg" in files[n][res] else "png"
                url = files[n][res][fmt]["url"]
                p = get(url, os.path.join(dl, asset, os.path.basename(url)))
                maps[key] = open_image(open(p, "rb").read())
                break
    return maps


def ambientcg(asset, res, dl):
    res = res.upper()
    zp = get(f"https://ambientcg.com/get?file={asset}_{res}-PNG.zip", os.path.join(dl, f"{asset}_{res}-PNG.zip"))
    suffix = {"albedo": "_Color.png", "normal": "_NormalGL.png", "roughness": "_Roughness.png",
              "ao": "_AmbientOcclusion.png", "height": "_Displacement.png", "metal": "_Metalness.png",
              "opacity": "_Opacity.png"}
    maps = {}
    with zipfile.ZipFile(zp) as z:
        names = z.namelist()
        for key, suf in suffix.items():
            hit = [n for n in names if n.endswith(suf) and "/" not in n and "\\" not in n]
            if hit:
                maps[key] = open_image(z.read(hit[0]))
    return maps


def srgb_to_lin(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055)


def recolour(im, hexcol, keep_sat, contrast, gain=1.0):
    a = srgb_to_lin(np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0)
    lum = a @ np.array([0.2126, 0.7152, 0.0722])
    rel = (lum / max(lum.mean(), 1e-6)) ** contrast
    rel = rel / rel.mean()
    if hexcol:
        t = srgb_to_lin(np.array([int(hexcol[i:i + 2], 16) / 255.0 for i in (1, 3, 5)]))
        out = rel[..., None] * t
    else:  # only lower the saturation, about each pixel's own grey
        out = lum[..., None] + keep_sat * (a - lum[..., None])
    out = out * np.asarray(gain, dtype=np.float64)
    return Image.fromarray((lin_to_srgb(out) * 255 + 0.5).astype(np.uint8))


def grey(im):
    if im.mode in ("I;16", "I;16B", "I", "F"):
        a = np.asarray(im, dtype=np.float64)
        a = a / (65535.0 if a.max() > 255 else 255.0)
        return Image.fromarray((np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8))
    return im.convert("L")


def make(name, row, out):
    dl = os.path.join(out, "_dl")
    site, asset = row["site"], row["asset"]
    res = row.get("res", "1k" if site == "ph" else "1K")
    maps = polyhaven(asset, res, dl) if site == "ph" else ambientcg(asset, res, dl)
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    alb = maps["albedo"].convert("RGB")
    hexcol, sat, contrast, gain = row.get("recolour"), row.get("saturation", 1.0), row.get("contrast", 1.0), row.get("gain", 1.0)
    if hexcol or sat < 1.0 or contrast != 1.0 or gain != 1.0:
        alb = recolour(alb, hexcol, sat, contrast, gain)
    if row.get("target"):
        a = srgb_to_lin(np.asarray(alb, dtype=np.float64) / 255.0)
        a = a * (np.asarray(row["target"]) / np.maximum(a.reshape(-1, 3).mean(axis=0), 1e-6))
        alb = Image.fromarray((lin_to_srgb(a) * 255 + 0.5).astype(np.uint8))
    alb.save(os.path.join(d, "albedo.jpg"), quality=93)
    maps["normal"].convert("RGB").save(os.path.join(d, "normal_gl.png"), optimize=True)
    size = alb.size
    rough = grey(maps["roughness"]) if "roughness" in maps else None
    ao = grey(maps["ao"]) if "ao" in maps else None
    metal = grey(maps["metal"]) if "metal" in maps else None
    if "arm" in maps:
        r, g, b = maps["arm"].convert("RGB").split()
        ao = ao if ao is not None else r
        rough = rough if rough is not None else g
        metal = metal if metal is not None else b
    if row.get("rough") is not None and rough is not None:
        r = np.asarray(rough.resize(size), dtype=np.float64) / 255.0
        rough = Image.fromarray((np.clip(r - r.mean() + row["rough"], 0, 1) * 255 + 0.5).astype(np.uint8))
    if not row.get("metal", False):
        metal = None
    fit = lambda im, fill: (im if im is not None else Image.new("L", size, fill)).resize(size)
    Image.merge("RGB", (fit(ao, 255), fit(rough, 200), fit(metal, 0))).save(os.path.join(d, "arm.png"), optimize=True)
    for key, im in (("roughness", rough), ("ao", ao), ("metal", metal)):
        if im is not None:
            im.save(os.path.join(d, key + ".png"), optimize=True)
    if "height" in maps:
        grey(maps["height"]).save(os.path.join(d, "height.png"), optimize=True)
    if "opacity" in maps:
        grey(maps["opacity"]).save(os.path.join(d, "opacity.png"), optimize=True)
    a = srgb_to_lin(np.asarray(alb, dtype=np.float64) / 255.0).reshape(-1, 3).mean(axis=0)
    print(f"{name} <- {site} {asset} {res}: albedo mean {a[0]:.3f} {a[1]:.3f} {a[2]:.3f}")


if __name__ == "__main__":
    table_path, out = sys.argv[1], sys.argv[2]
    table = json.load(open(table_path))
    for n in sys.argv[3:] or table:
        make(n, table[n], out)
