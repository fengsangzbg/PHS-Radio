"""Assemble the captured meteor frames into an animated GIF."""

import glob
import sys

from PIL import Image

SOURCE = sys.argv[1]
OUT = sys.argv[2]
WIDTH = 810
STEP_MS = 45


def main():
    files = sorted(glob.glob(f"{SOURCE}/frame_*.png"))
    if not files:
        raise SystemExit("no frames")
    frames = []
    for path in files:
        img = Image.open(path).convert("RGB")
        height = int(img.height * WIDTH / img.width)
        frames.append(img.resize((WIDTH, height), Image.LANCZOS))
    frames[0].save(
        OUT,
        save_all=True,
        append_images=frames[1:],
        duration=STEP_MS,
        loop=0,
        optimize=True,
    )
    print(f"{OUT}: {len(frames)} frames, {frames[0].size}")


if __name__ == "__main__":
    main()
