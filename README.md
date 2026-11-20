# OunceCord
WIP Discord Client for Switch 2 (KaiHEN)

> [!WARNING]
>
> All 3rd-party clients are technically against Discord's ToS and could result in your account being banned. while this app pretends to be Discord as much as possible. there is still a bgi risk.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
- Sending messages
- Reactions under messages
- Sending screenshots and video clips from the console's album
- Playing videos in chat (MP4/MOV with H.264, with sound) on the console's hardware video decoder.

## Building

Install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then run:

```sh
make
```

This produces `OunceCord.nro`. Copy it to `sdmc:/switch/` and launch it from hbmenu.

The fonts and icons are built in from `data/assets.bin`. After changing the icon list in
`tools/pack_assets.py` (or the files in `font/` and `assets/`), regenerate it with
`python3 tools/pack_assets.py`.

## Signing in

Log in with your Discord email (or phone number) and password.

If your account has two-factor authentication, choose an
authenticator app code, a text message code or a backup code next.

## Third-party code

Sound in videos is decoded by [fdk-aac](https://github.com/mstorsjo/fdk-aac) (v2.0.3, decoder only), in
`third_party/fdk-aac` under the Fraunhofer FDK AAC Codec Library license (see `third_party/fdk-aac/NOTICE`).
It is built by the normal `make`.

Emoji names and categories come from GitHub's [gemoji](https://github.com/github/gemoji) (MIT), packed into `data/assets.bin`; emoji images are [Twemoji](https://github.com/jdecked/twemoji) (CC-BY 4.0), loaded from jsDelivr.

Special Thanks to: [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
