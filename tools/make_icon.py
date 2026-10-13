"""Draws the program icon and writes assets/icon/Geisterhand.{png,icns,ico}.

    python3 tools/make_icon.py        (needs Pillow; the .icns step needs macOS)

A steering wheel, and behind it the same wheel turned further: the replay's ghost.
"""
import math
import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parents[1] / "assets" / "icon"
SIZE, SS = 1024, 4                      # drawn four times as large, then scaled down
BG, WHEEL, GHOST, MARK = (14, 15, 18), (236, 238, 242), (124, 231, 245), (255, 90, 95)


def wheel(draw, centre, radius, degrees, colour, rim, spoke, mark, solid):
    cx, cy = centre
    turn = math.radians(degrees)
    draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], outline=colour, width=rim)
    hub = radius * 0.27
    for at in (-math.pi / 2, math.pi / 2, math.pi):
        dx, dy = math.sin(turn + at), -math.cos(turn + at)
        draw.line([cx + dx * hub, cy + dy * hub, cx + dx * (radius - rim / 2), cy + dy * (radius - rim / 2)], fill=colour, width=spoke)
    box = [cx - hub, cy - hub, cx + hub, cy + hub]
    if solid:
        draw.ellipse(box, fill=colour)
    else:
        draw.ellipse(box, outline=colour, width=spoke // 2)
    dx, dy = math.sin(turn), -math.cos(turn)
    draw.line([cx + dx * (radius - rim * 1.05), cy + dy * (radius - rim * 1.05), cx + dx * (radius + 2), cy + dy * (radius + 2)], fill=mark, width=int(rim * 0.8))


def main():
    big = SIZE * SS
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    pad = int(big * 0.098)              # the margin macOS icons keep
    draw.rounded_rectangle([pad, pad, big - pad, big - pad], radius=int(big * 0.185), fill=BG)
    centre, radius = (big / 2, big / 2), big * 0.285
    wheel(draw, centre, radius, 38, GHOST, int(big * 0.012), int(big * 0.012), GHOST, False)
    wheel(draw, centre, radius, -8, WHEEL, int(big * 0.05), int(big * 0.04), MARK, True)
    icon = image.resize((SIZE, SIZE), Image.LANCZOS)

    OUT.mkdir(parents=True, exist_ok=True)
    icon.save(OUT / "Geisterhand.png")
    icon.save(OUT / "Geisterhand.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])

    if shutil.which("iconutil"):
        iconset = OUT / "Geisterhand.iconset"
        iconset.mkdir(exist_ok=True)
        for points in (16, 32, 128, 256, 512):
            icon.resize((points, points), Image.LANCZOS).save(iconset / f"icon_{points}x{points}.png")
            icon.resize((points * 2, points * 2), Image.LANCZOS).save(iconset / f"icon_{points}x{points}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(OUT / "Geisterhand.icns")], check=True)
        shutil.rmtree(iconset)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
