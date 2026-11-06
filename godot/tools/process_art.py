"""Turns the Higgsfield source sheets in godot/assets/source/ into game-ready
assets under godot/assets/. Re-run after replacing a sheet:

    python3 godot/tools/process_art.py

Needs Pillow (pip install pillow). Sheets are cut by finding each sprite as
a connected blob (generated sheets don't follow an exact grid), then trimmed,
alpha-cleaned and downscaled. Floor textures are made seamless.
"""
import os
import sys
from collections import deque
from PIL import Image, ImageChops, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
SRC = os.path.join(ROOT, "source")


def find_blobs(im, threshold=96, min_cells=60, grow=5):
    """Bounding boxes of separate sprites on a transparent sheet. `grow`
    (odd) bridges small gaps inside one sprite; lower it for tight sheets."""
    w, h = im.size
    s = 4
    mask = im.getchannel("A").resize((w // s, h // s), Image.BILINEAR)
    mask = mask.point(lambda v: 255 if v >= threshold else 0)
    if grow > 1:
        mask = mask.filter(ImageFilter.MaxFilter(grow))
    sw, sh = mask.size
    px = mask.load()
    seen = bytearray(sw * sh)
    boxes = []
    for y in range(sh):
        for x in range(sw):
            if px[x, y] and not seen[y * sw + x]:
                q = deque([(x, y)])
                seen[y * sw + x] = 1
                x0 = x1 = x
                y0 = y1 = y
                n = 0
                while q:
                    cx, cy = q.popleft()
                    n += 1
                    x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                    for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                        if 0 <= nx < sw and 0 <= ny < sh and px[nx, ny] and not seen[ny * sw + nx]:
                            seen[ny * sw + nx] = 1
                            q.append((nx, ny))
                if n >= min_cells:
                    boxes.append((x0 * s, y0 * s, (x1 + 1) * s, (y1 + 1) * s))
    return boxes


def order_rows(boxes, row_counts):
    """Row-major order given how many sprites each row holds."""
    if len(boxes) != sum(row_counts):
        raise SystemExit(f"found {len(boxes)} sprites, expected {sum(row_counts)}: {boxes}")
    boxes = sorted(boxes, key=lambda b: (b[1] + b[3]) / 2)
    out, i = [], 0
    for n in row_counts:
        out += sorted(boxes[i:i + n], key=lambda b: b[0])
        i += n
    return out


def cut(im, box, pad=6, opaque=False):
    """Crop a sprite, snap near-opaque alpha to 255, drop faint haze, trim."""
    w, h = im.size
    x0, y0, x1, y1 = box
    crop = im.crop((max(0, x0 - pad), max(0, y0 - pad), min(w, x1 + pad), min(h, y1 + pad)))
    if opaque:
        return crop
    a = crop.getchannel("A").point(lambda v: 255 if v >= 200 else (0 if v < 40 else v))
    crop.putalpha(a)
    bbox = a.point(lambda v: 255 if v >= 40 else 0).getbbox()
    return crop.crop(bbox) if bbox else crop


def fit(img, longest):
    """Downscale so the longest side is `longest` px."""
    scale = longest / max(img.size)
    if scale >= 1.0:
        return img
    return img.resize((max(1, round(img.size[0] * scale)), max(1, round(img.size[1] * scale))), Image.LANCZOS)


def seamless(tile):
    """Blend a texture with its half-offset copy so its edges wrap seamlessly."""
    w, h = tile.size
    shifted = ImageChops.offset(tile, w // 2, h // 2)
    mask = Image.new("L", (w, h))
    mp = mask.load()
    ramp = 0.22
    for y in range(h):
        for x in range(w):
            d = min(x, w - 1 - x, y, h - 1 - y) / (min(w, h) * ramp)
            d = max(0.0, min(1.0, d))
            mp[x, y] = int(255 * d * d * (3 - 2 * d))   # smoothstep: edges use the shifted copy
    return Image.composite(tile, shifted, mask)


def strengthen(img):
    """Make a faint decal readable: darken its lines and boost their alpha."""
    r, g, b, a = img.split()
    a = a.point(lambda v: min(255, int(v * 2.6)))
    dark = Image.new("RGB", img.size, (22, 22, 26))
    out = dark.copy()
    out.putalpha(a)
    return out


def save(img, *parts):
    path = os.path.join(ROOT, *parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, optimize=True)
    print(f"  {os.path.relpath(path, ROOT)}  {img.size[0]}x{img.size[1]}")


def main():
    # ── Vehicles: 3 + 3, all facing right ─────────────────────────
    sheet = Image.open(os.path.join(SRC, "vehicles_sheet.png")).convert("RGBA")
    names = ["Compact", "Standard", "SUV", "Motorcycle", "Disabled", "DeliveryVan"]
    print("vehicles:")
    for name, box in zip(names, order_rows(find_blobs(sheet), [3, 3])):
        save(fit(cut(sheet, box), 128), "vehicles", name + ".png")

    # ── Staff (3) + incident icons (5 + 5) ─────────────────────────
    sheet = Image.open(os.path.join(SRC, "staff_icons_sheet.png")).convert("RGBA")
    names = ["attendant", "security", "maintenance",
             "OilSpill", "FenderBender", "OverstayVehicle", "PowerOutage", "SuspiciousVehicle",
             "MedicalEmergency", "TheftInProgress", "StructuralCrack", "Altercation", "PipeBurst"]
    print("staff + icons:")
    for i, (name, box) in enumerate(zip(names, order_rows(find_blobs(sheet, grow=1), [3, 5, 5]))):
        folder = "staff" if i < 3 else "icons"
        save(fit(cut(sheet, box), 96), folder, name + ".png")

    # ── Environment: 3 square textures + 4 props ───────────────────
    sheet = Image.open(os.path.join(SRC, "environment_sheet.png")).convert("RGBA")
    boxes = order_rows(find_blobs(sheet, threshold=40, min_cells=40), [3, 4])
    print("environment:")
    for name, box in zip(["concrete", "asphalt", "wall"], boxes[:3]):
        x0, y0, x1, y1 = box
        side = min(x1 - x0, y1 - y0)
        inset = int(side * 0.06)          # drop the drawn frame and rounded corners
        square = sheet.crop((x0 + inset, y0 + inset, x0 + side - inset, y0 + side - inset)).convert("RGB")
        square = square.resize((256, 256), Image.LANCZOS)
        if name != "wall":                # the wall keeps its hazard stripe on one edge
            square = seamless(square)
        save(square, "tiles", name + ".png")
    for name, box in zip(["gate", "oil_stain", "crack", "pillar"], boxes[3:]):
        prop = cut(sheet, box)
        if name == "crack":
            prop = strengthen(prop)
        save(fit(prop, 192 if name == "gate" else 128), "props", name + ".png")

    # ── Menu key art ───────────────────────────────────────────────
    art = Image.open(os.path.join(SRC, "menu_keyart.png")).convert("RGB")
    path = os.path.join(ROOT, "ui", "menu_background.jpg")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    art.save(path, quality=88, optimize=True)
    print(f"  ui/menu_background.jpg  {art.size[0]}x{art.size[1]}")


if __name__ == "__main__":
    sys.exit(main())
