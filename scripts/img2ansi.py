#!/usr/bin/env python3
"""Convert an image to colored letter-ASCII art (ANSI truecolor) for fastfetch."""
import sys
from PIL import Image

SRC, DST, COLS = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 46
RAMP = " .,:;iltfLCGmwW@"   # dark -> bright, letters (LFS-ish)

img = Image.open(SRC).convert("RGB")

# auto-crop: bounding box of non-black content + small margin
gray = img.convert("L")
bbox = gray.point(lambda p: 255 if p > 22 else 0).getbbox()
if bbox:
    x0, y0, x1, y1 = bbox
    mx, my = (x1 - x0) // 20, (y1 - y0) // 20
    img = img.crop((max(0, x0 - mx), max(0, y0 - my),
                    min(img.width, x1 + mx), min(img.height, y1 + my)))

rows = max(1, round(COLS * img.height / img.width * 0.46))
img = img.resize((COLS, rows), Image.LANCZOS)

chars = [[" "] * COLS for _ in range(rows)]
colors = [[None] * COLS for _ in range(rows)]
for y in range(rows):
    for x in range(COLS):
        r, g, b = img.getpixel((x, y))
        lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
        if lum < 28:            # shadow noise stays background
            continue
        idx = 1 + int((lum - 28) / (256 - 28) * (len(RAMP) - 1))
        chars[y][x] = RAMP[min(len(RAMP) - 1, idx)]
        # boost color so dim pixels keep their hue on console
        mx = max(r, g, b)
        if 0 < mx < 140:
            f = 140 / mx
            r, g, b = min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f))
        colors[y][x] = (r, g, b)

# despeckle: drop cells whose neighbours are all background
for y in range(rows):
    for x in range(COLS):
        if chars[y][x] == " ":
            continue
        alone = True
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == dy == 0:
                    continue
                ny, nx = y + dy, x + dx
                if 0 <= ny < rows and 0 <= nx < COLS and chars[ny][nx] != " ":
                    alone = False
        if alone:
            chars[y][x] = " "

out = []
for y in range(rows):
    line, prev = [], None
    for x in range(COLS):
        if chars[y][x] == " ":
            line.append(" ")
            continue
        if colors[y][x] != prev:
            line.append("\033[38;2;%d;%d;%dm" % colors[y][x])
            prev = colors[y][x]
        line.append(chars[y][x])
    out.append("".join(line).rstrip() + "\033[0m")
while out and out[-1] == "\033[0m":
    out.pop()

open(DST, "w").write("\n".join(out) + "\n")
print("wrote %s: %d cols x %d rows" % (DST, COLS, len(out)))
