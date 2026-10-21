#!/usr/bin/env python3
"""Generate the hicolor app icons for Dakkar from data/icons/source/.

The source already has a transparent background: only trim to the
content, pad to a square with a small margin and resample.
"""
import os
from PIL import Image, ImageFilter
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = Image.open(os.path.join(root, 'data/icons/source/dakkar-icon-original.png')).convert('RGBA')
src = src.crop(src.getchannel('A').getbbox())
side = int(max(src.size) * 1.06)            # ~3% margin on each side
canvas = Image.new('RGBA', (side, side), (0, 0, 0, 0))
canvas.paste(src, ((side - src.width) // 2, (side - src.height) // 2), src)
SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]
for s in SIZES:
    im = canvas.resize((s, s), Image.LANCZOS)
    if s <= 48:
        # light sharpening keeps the gold border crisp at small sizes
        rgb = im.convert('RGB').filter(ImageFilter.UnsharpMask(radius=0.6, percent=60, threshold=1))
        rgb.putalpha(im.getchannel('A'))
        im = rgb
    d = os.path.join(root, 'data/icons/hicolor/apps/%dx%d' % (s, s))
    os.makedirs(d, exist_ok=True)
    im.save(os.path.join(d, 'org.dakkar.Files.png'), optimize=True)
