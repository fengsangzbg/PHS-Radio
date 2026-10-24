"""Stack the water and fire captures into one animated comparison GIF."""

import glob
import sys

from PIL import Image, ImageDraw

WATER, FIRE, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
WIDTH = 880
STEP_MS = 42
GAP = 6


def load(pattern, width):
    frames = []
    for path in sorted(glob.glob(f"{pattern}/frame_*.png")):
        img = Image.open(path).convert("RGB")
        height = int(img.height * width / img.width)
        frames.append(img.resize((width, height), Image.LANCZOS))
    return frames


def main():
    water = load(WATER, WIDTH)
    fire = load(FIRE, WIDTH)
    count = min(len(water), len(fire))
    tile_h = water[0].height
    canvas_h = tile_h * 2 + GAP
    out = []
    for i in range(count):
        canvas = Image.new("RGB", (WIDTH, canvas_h), (10, 13, 18))
        canvas.paste(water[i], (0, 0))
        canvas.paste(fire[i], (0, tile_h + GAP))
        draw = ImageDraw.Draw(canvas)
        draw.text((10, 6), "WATER", fill=(255, 255, 255))
        draw.text((10, tile_h + GAP + 6), "FIRE", fill=(255, 255, 255))
        out.append(canvas)
    out[0].save(OUT, save_all=True, append_images=out[1:], duration=STEP_MS, loop=0, optimize=True)
    print(f"{OUT}: {len(out)} frames")


if __name__ == "__main__":
    main()
