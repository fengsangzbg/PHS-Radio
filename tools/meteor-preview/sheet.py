"""Compose a compact before/after style sheet from the meteor filmstrips."""

import sys
from PIL import Image, ImageDraw, ImageFont

PAD = 12
TILE_H = 249
LABEL = 20
STEP = TILE_H + LABEL


def frame(strip, index):
    return strip.crop((PAD, PAD + index * STEP, PAD + 1728, PAD + index * STEP + TILE_H))


def main():
    water_path, fire_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]
    water = Image.open(water_path).convert("RGBA")
    fire = Image.open(fire_path).convert("RGBA")

    picks = [3, 4, 5]
    rows = [("水光流星", water, i) for i in picks] + [("火流星", fire, i) for i in picks]

    width = 1620
    gap = 10
    tile_h = int(TILE_H * (width / 1728))
    height = len(rows) * (tile_h + gap) + gap

    canvas = Image.new("RGBA", (width, height), (12, 16, 22, 255))
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 20)
    except OSError:
        font = ImageFont.load_default()

    y = gap
    for label, strip, index in rows:
        img = frame(strip, index).resize((width, tile_h), Image.LANCZOS)
        canvas.alpha_composite(img, (0, y))
        draw.text((14, y + 8), label, font=font, fill=(255, 255, 255, 220))
        y += tile_h + gap

    canvas.convert("RGB").save(out_path, quality=95)
    print("saved", out_path, canvas.size)


if __name__ == "__main__":
    main()
