# OunceCord
Rudimentary Discord Client for Switch 2 (KaiHEN)

I really recommend using an alt account as 3rd party clients are against ToS and im not responsible if your account is suspended.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
- Sending messages

## Building

Install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then run:

```sh
make
```

This produces `OunceCord.nro`. Copy it to `sdmc:/switch/` and launch it from hbmenu.

## Signing in

Log in with your Discord email (or phone number) and password.

If your account has two-factor authentication, choose an
authenticator app code, a text message code or a backup code next.

Logging in gives OunceCord a session of its own, so it never shares a token with
another client. The session is kept in `token.txt` so you stay logged in;
**-** twice logs out, which also ends the session on Discord's side.

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

Special Thanks to: [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
