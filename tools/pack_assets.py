#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Packs the fonts, the icons the UI uses and the emoji table (from
# tools/data/gemoji.json, GitHub's gemoji, MIT) into data/assets.bin, which the
# build embeds (see source/util/assets.h). Run it from the repository root
# after changing the list below:  python3 tools/pack_assets.py
#
# Icons marked "mask" become white with their original alpha, so the app can
# tint them to the theme; "color" ones keep their colours. All are shrunk to
# at most 64 px.
import io
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = "assets/images/native/"
P = "assets/images/platforms/"

FONTS = {
    "font/normal": "font/ggsans-Normal.ttf",
    "font/semibold": "font/ggsans-Semibold.ttf",
    "font/italic": "font/ggsans-NormalItalic.ttf",
    "font/semibold-italic": "font/ggsans-SemiboldItalic.ttf",
}

ICONS = {
    # UI
    "hash": (N + "icons/ic_text_channel_16px@2x.png", "mask"),
    "voice": (N + "icons/ic_voice_channel_24px@2x.png", "mask"),
    "forum": (N + "icons/ic_chat_bubble_32px@2x.png", "mask"),
    "thread": (N + "icons/ic_thread_normal_24px@2x.png", "mask"),
    "group": (N + "icons/ic_group_dm@2x.png", "mask"),
    "chevron": (N + "icons/ic_arrow_down@2x.png", "mask"),
    "gear": (N + "chat_sidebar/ic_settings@2x.png", "mask"),
    "settings/appearance": (N + "icons/ic_wand@2x.png", "mask"),
    "settings/time": (N + "icons/user_profile/ic_clock@2x.png", "mask"),
    "settings/input": (N + "icons/ic_controller_24px@2x.png", "mask"),
    "settings/experimental": (N + "icons/ic_sparkle@2x.png", "mask"),
    "settings/about": (N + "icons/ic_information_24px@2x.png", "mask"),
    "settings/logout": (N + "icons/ic_leave_24px@2x.png", "mask"),
    "arrow/back": (N + "icons/ic_arrow_back_24px@2x.png", "mask"),
    "arrow/forward": (N + "icons/ic_arrow_forward_24px@2x.png", "mask"),
    "add": (N + "icons/ic_add_circle@2x.png", "mask"),
    "send": (N + "icons/ic_send@2x.png", "mask"),
    "reply": (N + "icons/ic_swipe_reply_24px@2x.png", "mask"),
    "close": (N + "icons/ic_close_16px@2x.png", "mask"),
    "play": (N + "icons/ic_play@2x.png", "mask"),
    "download": (N + "icons/ic_download_24px@2x.png", "mask"),
    "check": (N + "icons/ic_verified_icon_white_16px@2x.png", "mask"),
    "lock": (N + "icons/ic_lock_short_16px@2x.png", "mask"),
    "file": (N + "icons/ic_file_small_unknown@2x.png", "color"),
    "discord": (N + "main_tabs/Discord@2x.png", "mask"),
    "status/online": (N + "status/StatusOnline@2x.png", "color"),
    "status/idle": (N + "status/StatusIdle@2x.png", "color"),
    "status/dnd": (N + "status/StatusDND@2x.png", "color"),
    "status/offline": (N + "status/StatusOffline@2x.png", "color"),
    # Connected accounts, by Discord's connection type
    "conn/battlenet": (P + "img_account_sync_battlenet_light_and_dark.png", "color"),
    "conn/bluesky": (P + "img_account_sync_bluesky_light_and_dark.png", "color"),
    "conn/bungie": (P + "img_bungie_white.png", "mask"),
    "conn/crunchyroll": (P + "img_account_sync_crunchyroll_light_and_dark.png", "color"),
    "conn/domain": (P + "img_domain_light.png", "mask"),
    "conn/ebay": (P + "img_account_sync_ebay_light_and_dark.png", "color"),
    "conn/epicgames": (P + "img_account_sync_epic_dark_and_white.png", "color"),
    "conn/facebook": (P + "img_account_sync_facebook_light_and_dark.png", "color"),
    "conn/github": (P + "img_account_sync_github_white.png", "mask"),
    "conn/instagram": (P + "img_account_sync_instagram_light_and_dark.png", "color"),
    "conn/leagueoflegends": (P + "img_account_sync_league_of_legends_light_and_dark.png", "color"),
    "conn/mastodon": (P + "img_account_sync_mastodon_light_and_dark.png", "color"),
    "conn/paypal": (P + "img_account_sync_paypal_light_and_dark.png", "color"),
    "conn/playstation": (P + "img_account_sync_playstation_white.png", "mask"),
    "conn/reddit": (P + "img_account_sync_reddit_light_and_dark.png", "color"),
    "conn/riotgames": (P + "img_account_sync_riot_light_and_dark.png", "color"),
    "conn/roblox": (P + "img_roblox_white.png", "mask"),
    "conn/samsung": (P + "img_account_sync_samsung_light_and_dark.png", "color"),
    "conn/skype": (P + "img_account_sync_skype_light_and_dark.png", "color"),
    "conn/spotify": (P + "img_account_sync_spotify_light_and_dark.png", "color"),
    "conn/steam": (P + "img_account_sync_steam_white.png", "mask"),
    "conn/tiktok": (P + "img_account_sync_tiktok_dark.png", "color"),
    "conn/twitch": (P + "img_account_sync_twitch_light_and_dark.png", "color"),
    "conn/twitter": (P + "img_account_sync_x_white.png", "mask"),
    "conn/xbox": (P + "img_account_sync_xbox_white.png", "mask"),
    "conn/youtube": (P + "img_account_sync_youtube_light_and_dark.png", "color"),
    "conn/amazon-music": (P + "img_amazon_music.png", "color"),
}


def icon_png(path, mode):
    im = Image.open(os.path.join(ROOT, path)).convert("RGBA")
    im.thumbnail((64, 64), Image.LANCZOS)
    if mode == "mask":
        alpha = im.getchannel("A")
        im = Image.new("RGBA", im.size, (255, 255, 255, 0))
        im.putalpha(alpha)
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
    for name, (path, mode) in ICONS.items():
        entries.append(("icon/" + name, icon_png(path, mode)))
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
