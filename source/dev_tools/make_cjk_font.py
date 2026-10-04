#!/usr/bin/env python3
"""Build the bundled Simplified-Chinese subset font for AssaultCube.

Takes Noto Sans CJK SC out of NotoSansCJK-Regular.ttc and trims it down to the
characters the game can actually show:

  * ASCII + Latin-1 supplement
  * CJK punctuation and fullwidth forms
  * the full GB2312 hanzi set (6763 chars), so player chat can use rare glyphs

Usage:
    python3 source/dev_tools/make_cjk_font.py [--source TTC] [--output TTF]

Requires fonttools (`pip install fonttools`).
"""

import argparse
import os
import sys

# NotoSansCJK-Regular.ttc face order (verified on Ubuntu 26.04):
# 0=JP 1=KR 2=SC 3=TC 4=HK 5..9 = Mono variants
SC_FACE_INDEX = 2

# Non-hanzi ranges we always want: ASCII, Latin-1, CJK punctuation, fullwidth.
EXTRA_RANGES = [
    (0x0020, 0x007E),  # basic latin
    (0x00A0, 0x00FF),  # latin-1 supplement
    (0x2010, 0x203B),  # general punctuation (dashes, quotes, ellipsis)
    (0x3000, 0x303F),  # CJK symbols and punctuation
    (0xFF00, 0xFFEF),  # halfwidth and fullwidth forms
]


def gb2312_chars():
    """Every character encodable in GB2312 (6763 hanzi + symbols)."""
    chars = set()
    for lead in range(0xA1, 0xFF):
        for trail in range(0xA1, 0xFF):
            try:
                ch = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            chars.add(ch)
    return chars


def build_text_file(path):
    chars = gb2312_chars()
    for lo, hi in EXTRA_RANGES:
        chars.update(chr(c) for c in range(lo, hi + 1))
    # Drop control/whitespace that the subsetter would choke on or that can
    # never be drawn; keep plain space.
    chars.discard("\x00")
    text = "".join(sorted(chars))
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return len(chars)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source",
                    default="/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
    ap.add_argument("--output", default="packages/misc/fonts/AcZhSans.ttf")
    args = ap.parse_args()

    if not os.path.exists(args.source):
        sys.exit("source font not found: %s" % args.source)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    textfile = args.output + ".chars.txt"
    count = build_text_file(textfile)
    print("character set: %d glyphs" % count)

    from fontTools import subset

    options = subset.Options()
    options.layout_features = ["*"]
    options.drop_tables += ["DSIG"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.glyph_names = False
    options.font_number = SC_FACE_INDEX  # pick Noto Sans CJK SC from the .ttc

    font = subset.load_font(args.source, options)
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=open(textfile, encoding="utf-8").read())
    subsetter.subset(font)
    subset.save_font(font, args.output, options)
    font.close()
    os.remove(textfile)

    print("wrote %s (%.1f MB)" % (args.output, os.path.getsize(args.output) / 1e6))


if __name__ == "__main__":
    main()
