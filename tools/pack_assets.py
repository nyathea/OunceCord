#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Packs the fonts (Open Sans, OFL, plus backup fonts for symbols: see
# FALLBACK_FONTS), the icons the UI uses (Lucide, ISC, from
# the third_party/lucide submodule), the emoji pictures (Twemoji, CC-BY 4.0,
# from the third_party/twemoji submodule) and the emoji table (from
# tools/data/gemoji.json, GitHub's gemoji, MIT) into data/assets.bin, which the
# build embeds (see source/util/assets.h). Run it from the repository root
# after changing the lists below:  python3 tools/pack_assets.py
# (it needs Pillow, cairosvg and fontTools:  pip install cairosvg fonttools pillow)
#
# Icons are drawn white at 64 px, so the app can tint them to the theme.
import io
import os
import re
import struct
import sys

from PIL import Image

try:
    import cairosvg
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
except ImportError:
    sys.exit("pack_assets.py needs cairosvg and fontTools: pip install cairosvg fonttools pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LUCIDE = "third_party/lucide/icons/"
# Twemoji (CC-BY 4.0), the third_party/twemoji submodule: every emoji's 72 px
# picture, packed as "twemoji/<file name>" (e.g. "twemoji/1f642").
TWEMOJI = "third_party/twemoji/assets/72x72/"

FONTS = {
    "font/normal": "font/OpenSans-Regular.ttf",
    "font/semibold": "font/OpenSans-SemiBold.ttf",
    "font/italic": "font/OpenSans-Italic.ttf",
    "font/semibold-italic": "font/OpenSans-SemiBoldItalic.ttf",
}

# Backup fonts for characters Open Sans lacks, tried in this order before the
# console's own fonts (which cover Japanese, Chinese and Korean). Each is cut
# down to the ranges listed, to keep assets.bin small. Emoji aren't here: the
# app draws them as Twemoji pictures.
FALLBACK_FONTS = [
    # DejaVu Sans (Bitstream Vera licence): symbols, arrows, box drawing
    # (the table flip), upside-down text and small caps.
    ("font/fallback/symbols", "font/fallback/DejaVuSans.ttf", [
        (0x0180, 0x024F),  # Latin extended B (upside-down letters)
        (0x0250, 0x036F),  # IPA letters, modifier letters, combining marks
        (0x1D00, 0x1DBF),  # phonetic extensions (small caps)
        (0x2000, 0x2BFF),  # punctuation ... miscellaneous symbols and arrows
        (0x2E00, 0x2E7F),  # supplemental punctuation
        (0xA720, 0xA7FF),  # Latin extended D (more small caps)
        (0xFFFD, 0xFFFD),
    ]),
    # Noto Sans Math (OFL): "fancy" letters (mathematical alphanumerics).
    ("font/fallback/math", "font/fallback/NotoSansMath-Regular.ttf", [
        (0x2100, 0x214F),
        (0x1D400, 0x1D7FF),
    ]),
    # Noto Sans Symbols (OFL), regular weight: circled and boxed letters.
    ("font/fallback/enclosed", "font/fallback/NotoSansSymbols[wght].ttf", [
        (0x2300, 0x23FF),
        (0x2460, 0x24FF),
        (0x1F100, 0x1F1E5),  # stops before the regional indicators (flags)
    ]),
]


def subset_font(path, ranges):
    font = TTFont(os.path.join(ROOT, path))
    if "fvar" in font:
        font = instancer.instantiateVariableFont(font, {"wght": 400})
    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]  # keeps the copyright and licence notices
    opts.notdef_outline = True
    opts.drop_tables += ["MATH", "FFTM"]  # unused here
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=[c for a, b in ranges for c in range(a, b + 1)])
    sub.subset(font)
    out = io.BytesIO()
    font.save(out)
    return out.getvalue()


# Lucide icon names (third_party/lucide/icons/<name>.svg). "fill" also fills
# the shape instead of only drawing its outline.
ICONS = {
    "hash": "hash",
    "voice": "volume-2",
    "forum": "messages-square",
    "thread": "message-square-text",
    "group": "users",
    "chevron": "chevron-down",
    "gear": "settings",
    "mic": "mic",
    "mic-off": "mic-off",
    "headphones": "headphones",
    "headphone-off": "headphone-off",
    "phone-off": "phone-off",
    "home": "message-circle",
    "settings/appearance": "palette",
    "settings/time": "clock",
    "settings/input": "gamepad-2",
    "settings/experimental": "flask-conical",
    "settings/about": "info",
    "settings/logout": "log-out",
    "arrow/back": "arrow-left",
    "arrow/forward": "chevron-right",
    "add": "circle-plus",
    "send": "send-horizontal",
    "reply": "reply",
    "close": "x",
    "play": ("play", "fill"),
    "download": "download",
    "check": "check",
    "lock": "lock",
    "file": "file",
}


def icon_png(name, mode=None):
    """A Lucide icon as a 64 px white PNG (the app tints it)."""
    with open(os.path.join(ROOT, LUCIDE + name + ".svg"), encoding="utf-8") as f:
        svg = f.read()
    svg = svg.replace("currentColor", "#ffffff")
    if mode == "fill":
        svg = re.sub(r'fill="none"', 'fill="#ffffff"', svg, count=1)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=64, output_height=64)
    im = Image.open(io.BytesIO(png)).convert("RGBA")
    out = io.BytesIO()
    im.save(out, "PNG", optimize=True)
    return out.getvalue()


# Discord's names for some emoji where they differ from gemoji's (listed
# first, so they're what the picker shows).
DISCORD_NAMES = {
    "🧑": ["person"], "🙂": ["slight_smile"], "🙃": ["upside_down"], "🤗": ["hugging"],
    "🤓": ["nerd"], "🙁": ["slight_frown"], "🤔": ["thinking"], "🤣": ["rofl"],
    "😶": ["no_mouth"], "🤐": ["zipper_mouth"], "🤒": ["thermometer_face"], "🤕": ["head_bandage"],
    "🤑": ["money_mouth"], "🤖": ["robot"], "🫡": ["saluting_face"], "👍": ["thumbsup"],
    "👎": ["thumbsdown"], "❤️": ["heart"], "💯": ["100"], "🔥": ["fire"], "💀": ["skull"],
    "😭": ["sob"], "🙏": ["pray"], "👀": ["eyes"], "🥺": ["pleading_face"], "🤡": ["clown"],
}


def emoji_table():
    """The emoji table for source/discord/emoji.h, from tools/data/gemoji.json."""
    import json
    with open(os.path.join(ROOT, "tools", "data", "gemoji.json"), encoding="utf-8") as f:
        data = json.load(f)
    cats = []
    lines = []
    for e in data:
        if e["category"] not in cats:
            cats.append(e["category"])
        names = DISCORD_NAMES.get(e["emoji"], []) + [a for a in e["aliases"] if a not in DISCORD_NAMES.get(e["emoji"], [])]
        tags = " ".join(e.get("tags", []) + e.get("description", "").split())
        lines.append("%s\t%d\t%s\t%s" % (e["emoji"], cats.index(e["category"]), ",".join(names), tags))
    head = ["#category\t" + c for c in cats]
    return ("\n".join(head + lines) + "\n").encode("utf-8")


def main():
    entries = []
    entries.append(("emoji/table", emoji_table()))
    for name, path in FONTS.items():
        with open(os.path.join(ROOT, path), "rb") as f:
            entries.append((name, f.read()))
    for name, path, ranges in FALLBACK_FONTS:
        entries.append((name, subset_font(path, ranges)))
    for fn in sorted(os.listdir(os.path.join(ROOT, TWEMOJI))):
        if fn.endswith(".png"):
            with open(os.path.join(ROOT, TWEMOJI, fn), "rb") as f:
                entries.append(("twemoji/" + fn[:-4], f.read()))
    for name, spec in ICONS.items():
        lucide, mode = spec if isinstance(spec, tuple) else (spec, None)
        entries.append(("icon/" + name, icon_png(lucide, mode)))
    # "OCA1", count, then per entry: u16 name length, name, u32 size, data.
    blob = bytearray(b"OCA1" + struct.pack("<I", len(entries)))
    for name, data in entries:
        n = name.encode()
        blob += struct.pack("<H", len(n)) + n + struct.pack("<I", len(data)) + data
    out = os.path.join(ROOT, "data", "assets.bin")
    with open(out, "wb") as f:
        f.write(blob)
    print("wrote %s: %d entries, %d bytes" % (out, len(entries), len(blob)), file=sys.stderr)


if __name__ == "__main__":
    main()
