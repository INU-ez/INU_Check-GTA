"""gtacheck.ico — sizes 16..256 from src/icon.png (the user's picture); without it, a drawn SA-orange tile
with a white check mark.
    python gen_icon.py [source.png]   → src/gtacheck.ico
"""
from PIL import Image, ImageDraw
import os, sys

here = os.path.dirname(os.path.abspath(__file__))
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "icon.png")

def tile(size):
    s = size * 4  # supersample
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = s * 0.22
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=r, fill=(232, 138, 42, 255))          # SA orange
    w = max(s * 0.11, 2)
    pts = [(s * 0.22, s * 0.53), (s * 0.42, s * 0.73), (s * 0.79, s * 0.31)]
    d.line(pts, fill=(255, 255, 255, 255), width=int(w), joint="curve")
    for p in pts:
        d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=(255, 255, 255, 255))
    return im.resize((size, size), Image.LANCZOS)

sizes = [16, 24, 32, 48, 64, 128, 256]
if os.path.isfile(src):
    base = Image.open(src).convert("RGBA")
    if base.size[0] != base.size[1]:  # centre-crop to a square
        m = min(base.size); l = (base.size[0] - m) // 2; t = (base.size[1] - m) // 2
        base = base.crop((l, t, l + m, t + m))
    imgs = [base.resize((z, z), Image.LANCZOS) for z in sizes]
    print("source:", src, base.size)
else:
    imgs = [tile(z) for z in sizes]
    print("source: drawn tile")
out = os.path.join(here, "gtacheck.ico")
imgs[-1].save(out, format="ICO", sizes=[(z, z) for z in sizes], append_images=imgs[:-1])  # base 256, the rest as frames
print(out, os.path.getsize(out), "bytes")
