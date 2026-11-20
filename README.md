# OunceCord
WIP Discord Client for Switch 2 (KaiHEN)

> [!WARNING]
>
> All 3rd-party clients are technically against Discord's ToS and could result in your account being banned. while this app pretends to be Discord as much as possible. there is still a bgi risk.

## Current Features

- Log in with your email or phone number and password, with two-factor authentication
<<<<<<< HEAD
- Sending messages
- Reactions under messages
- Sending screenshots and video clips from the console's album
- Playing videos in chat (MP4/MOV with H.264, with sound) on the console's hardware video decoder.
=======
- Renders at 1080p on Switch 2, handheld and docked (720p on Switch 1)
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
- Forum and media channels (like a support or releases channel): a list of posts with their tags, first message, picture, reply count and latest activity (pinned posts first, older posts load as you scroll); open one to read and reply, B goes back to the posts
- Threads: the ones you've joined show under their channel, and messages that started a thread get a box to open it (B goes back to the channel)
- Server nicknames, and names coloured by each person's highest coloured role
- Profiles: tap someone's picture (or pick it with left/right) for their banner, bio, pronouns, badges, roles and connections, including their server profile
- Mentions shown as names (users, roles, channels, @everyone) plus timestamps, with messages that mention you highlighted
- Settings, in sections: Appearance (dark, light and AMOLED themes), Date & time (24-hour or 12-hour AM/PM, and YYYY-MM-DD, MM-DD-YYYY or DD-MM-YYYY dates), Input (mouse mode and the touch screen), Experimental (QR code login, videos) and About (system version, platform, app version and homebrew environment), plus Log out
- Animated GIFs, including Tenor and Giphy GIFs (fetched directly from Tenor/Giphy, since Discord only proxies those as video). Klipy GIFs show as a still for now
>>>>>>> c1793da (fix res)

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
