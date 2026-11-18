# OunceCord
Rudimentary Discord Client for Switch 2 (KaiHen) & Switch 1 (Atmosphere)

> [!WARNING]
> Third-party clients violate Discord's Terms of Service, and accounts can be
> banned for using them. Use at your own risk.

## Features (so far)

- Sign in with your account token (typed on the software keyboard, or read from the SD card)
- Servers, channels (filtered by your permissions) and direct messages
- Reading history, with older messages loaded as you scroll up
- Live messages, edits, deletions and typing indicators over the gateway
- Sending messages
- Profile pictures and server icons (cached on the SD card)
- Rendering with OpenGL ES (OunceVK) and the console's built-in fonts

## Building

Install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then:

```sh
make
```

This produces `OunceCord.nro`. Copy it to `sdmc:/switch/` and launch it from hbmenu.

## Signing in

Either press **A** on the login screen and enter your token, or put it in
`sdmc:/switch/OunceCord/token.txt` and press **Y**. The token is saved to that
file after a successful entry; **-** twice logs out and deletes it.

## Controls

| Button | Action |
| --- | --- |
| D-pad / left stick | Move selection, scroll messages |
| A | Open server / channel |
| B | Back |
| L / R | Previous / next server |
| X | Write a message |
| Right stick | Scroll messages (click to jump to latest) |
| ZL / ZR | Page up / down |
| Y | Reload messages |
| - (twice) | Log out |
| + | Exit |

Touch: tap servers and channels, drag to scroll, tap the message bar to write.

Mouse mode (Switch 2): put a Joy-Con 2 on its side on a surface and slide it. A cursor
appears; click with its L/ZL or R/ZR, hold and drag or use its stick to scroll, and press
the d-pad (or leave it for 5 seconds) to go back to button controls. While in mouse mode,
that Joy-Con's shoulder buttons and stick only act as the mouse; the other Joy-Con works
as usual.

## Files on the SD card

All in `sdmc:/switch/OunceCord/`:

- `token.txt`: the saved token (plain text: keep your SD card private)
- `log.txt`: the app log (never contains the token)
- `cache/`: downloaded avatars and icons (safe to delete; cleared automatically past 5,000 files)
- `font.ttf` / `font.otf` (optional): an extra font tried before the system fonts, for example one with emoji or wider Unicode coverage

## Layout

- `source/net/`: TLS over Horizon's `ssl` service, HTTP/1.1, WebSocket
- `source/discord/`: REST worker, gateway (zlib-stream), models and state
- `source/ui/`: GLES renderer with a FreeType glyph atlas, and the app UI
- `source/util/`: JSON, UTF-8, logging and helpers

Thanks to [Paicord](https://github.com/llsc12/Paicord) for the reference on how user clients talk to Discord.
