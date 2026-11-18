# OunceCord
Rudimentary Discord Client for Switch 2 (KaiHen)

I recommend using an alt account as 3rd party clients are against ToS and im not responsible if your account is suspended. you have been warned.


## Features (so far)

- Sign in with your account token (typed on the software keyboard, or read from the SD card)
- Servers, channels, and DMs
- Live messages, edits, deletions and typing indicators
- Sending messages

## Building

Install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then run:

```sh
make
```

This produces `OunceCord.nro`. Copy it to `sdmc:/switch/` and launch it from hbmenu.

## Signing in

Either press **A** on the login screen and enter your token, or put it in
`sdmc:/switch/OunceCord/token.txt`.

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

Mouse mode: put a Joy-Con 2 on its side on a surface and slide it. A cursor
appears; click with its L/ZL or R/ZR, hold and drag or use its stick to scroll, and press
the d-pad (or leave it for 5 seconds) to go back to button controls. While in mouse mode,
that Joy-Con's shoulder buttons and stick only act as the mouse; the other Joy-Con works
as usual.

Thanks to [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
