# OunceCord
Rudimentary Discord Client for Switch 2 (KaiHEN)

I really recommend using an alt account as 3rd party clients are against ToS and im not responsible if your account is suspended.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
- Sending messages, and replying to them
- Reactions under messages; tap one (or pick it in the X menu) to add yours to it or take yours back
- Images in chat (attachments and link previews) with a full-screen viewer
- Sending screenshots and video clips from the console's album, with or without a message (up to your upload limit: 20 MB, more with Nitro or a boosted server)
- Playing videos in chat (MP4/MOV with H.264, with sound) on the console's hardware video decoder
- Saving pictures from chat to `sdmc:/switch/OunceCord/Saved`, and sending pictures from that folder (put your own JPEG, PNG or GIF files there too)
- Discord's markdown: **bold**, *italics*, __underline__, ~~strikethrough~~, ||spoilers|| (tap them, or press A on the message, to reveal), `inline code` and code blocks (with syntax colouring for C/C++, C#, Java, Kotlin, Swift, JS/TS, Dart, Python, Rust, Go, Lua, PHP, Ruby, shell, SQL, diff, JSON, HTML/XML, CSS and YAML/TOML/INI), quotes, headings, subtext, lists and links
- Custom emotes, animated ones included, and standard emoji drawn as Twemoji (large when a message is only emotes, like Discord)
- An emoji picker (the smiley in the message bar, or "Add emoji" in the X menu) with search and a section per server. It only offers what you can send: this server's emotes and the standard emoji without Nitro; with Nitro, animated emotes and other servers' emotes too (where the channel allows external emotes)
- Typing `:shortcodes:` like `:person:`, `:thumbsup::skin-tone-2:` or a server emote's `:name:` in a message: they turn into the emoji when it's sent (emotes you can't use stay as text)
- Server nicknames, and names coloured by each person's highest coloured role
- Profiles: tap someone's picture (or pick it with left/right) for their banner, bio, pronouns, badges, roles and connections, including their server profile
- Mentions shown as names (users, roles, channels, @everyone) plus timestamps, with messages that mention you highlighted
- Settings: dark, light and AMOLED themes, turning mouse mode and the touch screen on or off, and an experimental QR code login, plus the system version, platform, app version and homebrew environment
- Animated GIFs, including Tenor and Giphy GIFs (fetched directly from Tenor/Giphy, since Discord only proxies those as video). Klipy GIFs show as a still for now

## Building

Install the Switch 2 devkit ([ounce-devkit](https://github.com/ouncebrew/ounce-devkit)), then run:

```sh
make
```

This produces `OunceCord.nro`. Copy it to `sdmc:/switch/` and launch it from hbmenu.

The fonts and icons are built in from `data/assets.bin`. After changing the icon list in
`tools/pack_assets.py` (or the files in `font/` and `assets/`), regenerate it with
`python3 tools/pack_assets.py` (needs Pillow). A `font.ttf` in `sdmc:/switch/OunceCord/` still
replaces the built-in font.

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
| D-pad / left stick | Move selection; in chat, up/down highlight messages one by one, and left/right move between the highlighted message's parts (author's picture, Reply, images, reactions). Down past the newest message reaches the text box row (attach, text, send, cancel reply); down past the last channel reaches the settings gear |
| A | Open / press whatever has focus: the author's picture shows their profile (banner, bio, pronouns, badges, member-since dates, mutual servers, server roles and connected accounts; up/down scroll it), Reply starts a reply and moves to the text box, a reaction adds or removes yours, an image opens in the viewer. On a whole message it views its image (or the newest image on screen) |
| B | Back |
| L / R | Previous / next server |
| X | Message menu: write/edit the message (fills the text box), attach or remove a picture, cancel a reply (L/R in the picker switch between Album and Saved) |
| Right stick | Highlight messages, faster the further you push (click to jump to latest) |
| ZL / ZR | Page up / down |
| Y | Send the message in the text box |
| - | Settings (also the gear next to your name); log out is at the bottom |
| + | Exit |

Video player: **A** plays/pauses (or replays at the end), **left/right** jump 5 seconds, **L/R** (or ZL/ZR) 15 seconds, **B** closes. With touch or the mouse, tap the bar to seek and anywhere else to play/pause.

Touch: tap servers and channels, drag to scroll (flick to glide; lists stretch and bounce back at the ends, like iOS; touch a gliding list to stop it), tap the message bar to write, the + to attach a screenshot, the x on it to remove it and the arrow to send, tap a message to highlight it and show its Reply button (mouse: just point at it), tap someone's picture for their profile, tap a reaction to add or remove yours, tap an image to view it (or reveal a spoiler). In the viewer, left/right (or L/R) go to the previous/next image, Y (or the Save button) saves it to the Saved folder, and B closes it.

Mouse mode: put a Joy-Con 2 on its side on a surface and slide it. A cursor
appears; click with its L/ZL or R/ZR, hold and drag or use its stick to scroll, and press
the d-pad (or leave it for 5 seconds) to go back to button controls. While in mouse mode,
that Joy-Con's shoulder buttons and stick only act as the mouse; the other Joy-Con works
as usual. Mouse mode and the touch screen can be turned off in settings.

## Third-party code

Sound in videos is decoded by [fdk-aac](https://github.com/mstorsjo/fdk-aac) (v2.0.3, decoder only), in
`third_party/fdk-aac` under the Fraunhofer FDK AAC Codec Library license (see `third_party/fdk-aac/NOTICE`).
It is built by the normal `make`.

Emoji names and categories come from GitHub's [gemoji](https://github.com/github/gemoji) (MIT), packed into `data/assets.bin`; emoji images are [Twemoji](https://github.com/jdecked/twemoji) (CC-BY 4.0), loaded from jsDelivr.

Special Thanks to: [Paicord](https://github.com/llsc12/Paicord) which was used as a reference.
