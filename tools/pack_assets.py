#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Packs the fonts (Open Sans, OFL), the icons the UI uses (Lucide, ISC, from
# the third_party/lucide submodule) and the emoji table (from
# tools/data/gemoji.json, GitHub's gemoji, MIT) into data/assets.bin, which the
# build embeds (see source/util/assets.h). Run it from the repository root
# after changing the lists below:  python3 tools/pack_assets.py
# (it needs Pillow and cairosvg:  pip install cairosvg pillow)
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
except ImportError:
    sys.exit("pack_assets.py needs cairosvg to draw the Lucide icons: pip install cairosvg pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LUCIDE = "third_party/lucide/icons/"

FONTS = {
    "font/normal": "font/OpenSans-Regular.ttf",
    "font/semibold": "font/OpenSans-SemiBold.ttf",
    "font/italic": "font/OpenSans-Italic.ttf",
    "font/semibold-italic": "font/OpenSans-SemiBoldItalic.ttf",
}

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
