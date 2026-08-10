#!/usr/bin/env python3
"""THE OBSERVER - asset import pipeline.

Turns raw generated images (assets/textures/src) into game-ready assets:
  * tileable wall/floor textures (offset-blend seamless conversion)
  * straight resizes for non-tiling surfaces
  * black-background figure sprites -> RGBA cutouts (Witness stages)
  * Steam store capsule set sliced from the key-art masters
Run from the observer/ directory:  python3 tools/import_assets.py
"""
import os
import sys

from PIL import Image, ImageFilter

SRC = "assets/textures/src"
OUT_TEX = "assets/textures"
OUT_WITNESS = "assets/witness"
OUT_STORE = "assets/store"

TILEABLE = {
    "00_wallpaper_hall": ("wallpaper_hall", 512),
    "01_wallpaper_apt": ("wallpaper_apt", 512),
    "02_plaster": ("plaster", 512),
    "03_concrete_wall": ("concrete_wall", 512),
    "04_carpet_hall": ("carpet_hall", 512),
    "05_parquet": ("parquet", 512),
    "06_linoleum": ("linoleum", 512),
    "07_ceiling_tile": ("ceiling_tile", 512),
    "08_brick_green": ("brick_green", 512),
    "09_concrete_floor": ("concrete_floor", 512),
    "12_bathroom_tile": ("bathroom_tile", 512),
    "13_cork_board": ("cork_board", 512),
    "15_curtain": ("curtain", 512),
    "16_wood_table": ("wood_table", 512),
    "17_bed_fabric": ("bed_fabric", 512),
}

PLAIN = {
    "10_door_wood": ("door_wood", 512, 1024),
    "11_elevator_doors": ("elevator_doors", 512, 1024),
    "14_paper_aged": ("paper_aged", 640, 854),
    "18_window_night": ("window_night", 640, 854),
    "19_breaker_panel": ("breaker_panel", 432, 768),
    "20_blueprint": ("blueprint", 640, 854),
    "21_protagonist": ("protagonist_photo", 512, 683),
    "36_photo_1961": ("photo_1961", 800, 600),
}

SPRITES = {  # black-background figures -> RGBA
    "30_witness_s1": "witness_s1",
    "31_witness_s2": "witness_s2",
    "32_witness_s3": "witness_s3",
    "33_witness_s4": "witness_s4",
}


def make_tileable(img: Image.Image, size: int) -> Image.Image:
    """Offset by half and hide the cross-seam under the original's center.

    The output's borders come from the offset copy's interior, so the result
    tiles perfectly; a radial mask keeps most of the original look.
    """
    img = img.convert("RGB").resize((size, size), Image.LANCZOS)
    off = Image.new("RGB", (size, size))
    h = size // 2
    off.paste(img.crop((h, h, size, size)), (0, 0))
    off.paste(img.crop((0, h, h, size)), (h, 0))
    off.paste(img.crop((h, 0, size, h)), (0, h))
    off.paste(img.crop((0, 0, h, h)), (h, h))
    # radial mask: original in the middle, offset near the borders
    mask = Image.new("L", (size, size), 0)
    px = mask.load()
    cx = cy = (size - 1) / 2.0
    maxd = (size / 2.0)
    for y in range(size):
        for x in range(size):
            dx = abs(x - cx) / maxd
            dy = abs(y - cy) / maxd
            d = max(dx, dy)  # square falloff matches the seam geometry
            v = 1.0 - min(1.0, max(0.0, (d - 0.55) / 0.38))
            px[x, y] = int(v * 255)
    mask = mask.filter(ImageFilter.GaussianBlur(size // 32))
    return Image.composite(img, off, mask)


def extract_sprite(img: Image.Image) -> Image.Image:
    """Black background -> alpha, despeckle film grain, crop, pad, resize."""
    import numpy as np

    img = img.convert("RGB")
    arr = np.asarray(img).astype(np.float32)
    m = arr.max(axis=2)
    alpha = np.clip((m - 14.0) / 32.0, 0.0, 1.0)
    # despeckle: kill alpha that has no solid support in its neighborhood
    k = 4
    pad = np.pad(alpha, k, mode="constant")
    windows = np.lib.stride_tricks.sliding_window_view(pad, (2 * k + 1, 2 * k + 1))
    support = windows.mean(axis=(2, 3))
    alpha = np.where(support > 0.22, alpha, 0.0)
    a8 = (alpha * 255).astype(np.uint8)
    rgba = np.dstack([arr.astype(np.uint8), a8])
    rgba = Image.fromarray(rgba, "RGBA")
    bbox = Image.fromarray(a8, "L").getbbox()
    if bbox:
        rgba = rgba.crop(bbox)
    # pad 6% and resize to 1024 tall
    w, h = rgba.size
    pad = int(h * 0.06)
    padded = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    padded.paste(rgba, (pad, pad))
    w, h = padded.size
    nh = 1024
    nw = max(8, int(w * nh / h))
    return padded.resize((nw, nh), Image.LANCZOS)


def cover_crop(img: Image.Image, tw: int, th: int) -> Image.Image:
    """Scale to cover tw x th, center crop."""
    w, h = img.size
    scale = max(tw / w, th / h)
    img2 = img.resize((int(w * scale + 0.5), int(h * scale + 0.5)), Image.LANCZOS)
    w2, h2 = img2.size
    x = (w2 - tw) // 2
    y = (h2 - th) // 2
    return img2.crop((x, y, x + tw, y + th))


STEAM_FROM_VERTICAL = {
    "library_600x900.png": (600, 900),
    "vertical_capsule_748x896.png": (748, 896),
}
STEAM_FROM_HORIZONTAL = {
    "header_460x215.png": (460, 215),
    "main_capsule_616x353.png": (616, 353),
    "small_capsule_231x87.png": (231, 87),
    "hero_3840x1240.png": (3840, 1240),
    "screenshot_placeholder_1920x1080.png": (1920, 1080),
}


def main():
    os.makedirs(OUT_TEX, exist_ok=True)
    os.makedirs(OUT_WITNESS, exist_ok=True)
    os.makedirs(OUT_STORE, exist_ok=True)
    done, missing = 0, []

    for src_name, (out_name, size) in TILEABLE.items():
        p = f"{SRC}/{src_name}.png"
        if not os.path.exists(p):
            missing.append(src_name)
            continue
        make_tileable(Image.open(p), size).save(f"{OUT_TEX}/{out_name}.png")
        done += 1

    for src_name, (out_name, tw, th) in PLAIN.items():
        p = f"{SRC}/{src_name}.png"
        if not os.path.exists(p):
            missing.append(src_name)
            continue
        cover_crop(Image.open(p).convert("RGB"), tw, th).save(f"{OUT_TEX}/{out_name}.png")
        done += 1

    for src_name, out_name in SPRITES.items():
        p = f"{SRC}/{src_name}.png"
        if not os.path.exists(p):
            missing.append(src_name)
            continue
        extract_sprite(Image.open(p)).save(f"{OUT_WITNESS}/{out_name}.png")
        done += 1

    # protagonist cutout for CCTV doppelganger: reuse witness_s4 silhouette
    # unless a dedicated cutout exists
    s4 = f"{OUT_WITNESS}/witness_s4.png"
    cut = f"{OUT_WITNESS}/protagonist.png"
    if os.path.exists(s4) and not os.path.exists(cut):
        Image.open(s4).save(cut)
        print("protagonist.png: using witness_s4 cutout (same likeness by design)")

    kv = f"{SRC}/34_keyart_vertical.png"
    if os.path.exists(kv):
        img = Image.open(kv).convert("RGB")
        img.save(f"{OUT_STORE}/keyart_vertical.png")
        for name, (tw, th) in STEAM_FROM_VERTICAL.items():
            cover_crop(img, tw, th).save(f"{OUT_STORE}/{name}")
        done += 1
    else:
        missing.append("34_keyart_vertical")
    kh = f"{SRC}/35_keyart_horizontal.png"
    if os.path.exists(kh):
        img = Image.open(kh).convert("RGB")
        img.save(f"{OUT_STORE}/keyart_horizontal.png")
        for name, (tw, th) in STEAM_FROM_HORIZONTAL.items():
            cover_crop(img, tw, th).save(f"{OUT_STORE}/{name}")
        done += 1
    else:
        missing.append("35_keyart_horizontal")

    print(f"imported {done} assets")
    if missing:
        print("missing sources (skipped):", ", ".join(missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
