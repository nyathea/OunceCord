# OunceCord
Rudimentary Discord Client for Switch 2 (KaiHEN)

I really recommend using an alt account as 3rd party clients are against ToS and im not responsible if your account is suspended.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
- Sending messages, and replying to them
- Reactions under messages; tap one (or pick it in the X menu) to add yours to it or take yours back
- Images in chat (attachments and link previews) with a full-screen viewer
- Sending screenshots from the console's album, with or without a message
- Saving pictures from chat to `sdmc:/switch/OunceCord/Saved`, and sending pictures from that folder (put your own JPEG, PNG or GIF files there too)
- Custom emotes, animated ones included (large when a message is only emotes, like Discord)
- Server nicknames, and names coloured by each person's highest coloured role
- Mentions shown as names (users, roles, channels, @everyone) plus timestamps, with messages that mention you highlighted
- Settings: dark, light and AMOLED themes, turning mouse mode and the touch screen on or off, and an experimental QR code login, plus the system version, platform, app version and homebrew environment
- Animated GIFs, including Tenor and Giphy GIFs (fetched directly from Tenor/Giphy, since Discord only proxies those as video). Klipy GIFs show as a still for now

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
| D-pad / left stick | Move selection; in chat, highlight messages one by one |
| A | Open server / channel; in chat, view the highlighted message's image (or the newest image on screen) |
| B | Back |
| L / R | Previous / next server |
| X | Menu: reply to the highlighted message, add to/remove its reactions (left/right to pick one), write/edit the message (fills the text box), attach or remove a picture (L/R in the picker switch between Album and Saved) |
| Right stick | Highlight messages, faster the further you push (click to jump to latest) |
| ZL / ZR | Page up / down |
| Y | Send the message in the text box |
| - | Settings (also the gear next to your name); log out is at the bottom |
| + | Exit |

Touch: tap servers and channels, drag to scroll, tap the message bar to write, the + to attach a screenshot, the x on it to remove it and the arrow to send, tap a message to highlight it and show its Reply button (mouse: just point at it), tap a reaction to add or remove yours, tap an image to view it (or reveal a spoiler). In the viewer, left/right (or L/R) go to the previous/next image, Y (or the Save button) saves it to the Saved folder, and B closes it.

Mouse mode: put a Joy-Con 2 on its side on a surface and slide it. A cursor
appears; click with its L/ZL or R/ZR, hold and drag or use its stick to scroll, and press
the d-pad (or leave it for 5 seconds) to go back to button controls. While in mouse mode,
that Joy-Con's shoulder buttons and stick only act as the mouse; the other Joy-Con works
as usual. Mouse mode and the touch screen can be turned off in settings.

Special Thanks to: [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
