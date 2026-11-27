# OunceCord
WIP Discord Client for Switch 2 (KaiHEN)

> [!WARNING]
>
> All 3rd-party clients are technically against Discord's ToS. while this app pretends to be Discord as much as possible. there is still a big risk to using this.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
- Sending messages
- Reactions under messages
- Sending screenshots and video clips from the console's album
- Playing videos in chat on the console's hardware video decoder.
- Voice channels: see who is in them, join, talk, mute (also the left stick click) and deafen, with
  Discord's end-to-end encryption (DAVE).

## Building

Clone with the Lucide and Twemoji submodules (`git clone --recursive`, or `git submodule update --init` in an
existing clone), install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then run:

```sh
make
```

The first `make` also downloads and cross-builds the voice libraries (libdave, MLS++ and OpenSSL) into
`.voice-deps/` with `tools/voice/build_deps.sh`. That needs `cmake`, `ninja`, `perl` and `curl`, and the Opus
portlib from devkitPro (`switch-libopus`). Later builds reuse them.

The fonts and icons are built in from `data/assets.bin`. After changing the font or icon lists in
`tools/pack_assets.py`, regenerate it with `python3 tools/pack_assets.py` (it needs
`pip install cairosvg fonttools pillow` to draw the icons and cut down the backup fonts).

## Signing in

Log in with your Discord email (or phone number) and password.

If your account has two-factor authentication, choose an
authenticator app code, a text message code or a backup code next.

## Third-party code

Sound in videos is decoded by [fdk-aac](https://github.com/mstorsjo/fdk-aac) (v2.0.3, decoder only), in
`third_party/fdk-aac` under the Fraunhofer FDK AAC Codec Library license (see `third_party/fdk-aac/NOTICE`).
It is built by the normal `make`.

Emoji names and categories come from GitHub's [gemoji](https://github.com/github/gemoji) (MIT), packed into `data/assets.bin`; emoji images are [Twemoji](https://github.com/jdecked/twemoji) v15.1.0 (graphics CC-BY 4.0), built in from the `third_party/twemoji` submodule.

Voice uses [libdave](https://github.com/discord/libdave) (MIT) for DAVE, with
[MLS++](https://github.com/cisco/mlspp) (BSD-2-Clause), [nlohmann/json](https://github.com/nlohmann/json) (MIT) and [OpenSSL](https://www.openssl.org/) 3 (Apache 2.0),
and [Opus](https://opus-codec.org/) (BSD-3-Clause) from devkitPro's portlibs. Parts of the voice code follow
DiscordNX2's voice engine (MIT).

Icons are from [Lucide](https://github.com/lucide-icons/lucide) (ISC), in the `third_party/lucide` submodule.
The font is [Open Sans](https://github.com/googlefonts/opensans) (SIL Open Font License 1.1). Symbols and
"fancy" letters it lacks come from cut-down copies of [DejaVu Sans](https://dejavu-fonts.github.io/)
(Bitstream Vera license), [Noto Sans Math](https://github.com/notofonts/math) and
[Noto Sans Symbols](https://github.com/notofonts/symbols) (both SIL Open Font License 1.1), in `font/fallback`
with their licenses.

Special Thanks to: [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
