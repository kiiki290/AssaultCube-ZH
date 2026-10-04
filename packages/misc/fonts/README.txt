Bundled Simplified Chinese font
===============================

AcZhSans.ttf is a subset of Noto Sans CJK SC (face index 2 of
NotoSansCJK-Regular.ttc), trimmed with fontTools to the characters the
game can display:

  * ASCII and Latin-1 supplement
  * General punctuation, CJK symbols/punctuation, fullwidth forms
  * the full GB2312 hanzi set (6763 characters)
  * the kana/Greek/Cyrillic letters GB2312 carries in its symbol rows

Characters outside this set -- rare hanzi, traditional-only forms,
hangul, emoji, CJK extensions -- have no glyph and render as blank.

Regenerate with:
    python3 source/dev_tools/make_cjk_font.py

Licensed under the SIL Open Font License 1.1; see LICENSE_OFL.txt.
