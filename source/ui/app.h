// SPDX-License-Identifier: MIT
//
// The OunceCord UI: login screen, then a three-pane layout (servers,
// channels, messages) driven by the controller or the touch screen.
#pragma once

#include <string>
#include <vector>

#include <switch.h>

#include "discord/client.h"
#include "ui/images.h"
#include "ui/render.h"

class App {
public:
    bool init();
    void exit();
    // Runs one frame. Returns false to quit.
    bool frame();

private:
    enum class Screen { Login, Main };
    enum class Pane { Guilds, Channels, Messages };

    struct ChannelRow {
        Snowflake id = 0;
        std::string label;
        bool header = false;  // category title, not selectable
        bool voice = false;   // shown, but cannot be opened
        std::string icon;     // CDN path for DMs
    };

    struct MessageLayout {
        Snowflake id = 0;
        Snowflake author_id = 0;
        std::string avatar;  // CDN path
        bool show_header = false;
        float height = 0;
        std::string author, time;
        std::vector<std::string> lines;
        std::vector<std::pair<std::string, Color>> extras;  // reply/attachments/embeds
        std::string reply;
        Color text_color;
    };

    Renderer m_r;
    ImageCache m_images;
    discord::Client m_client;
    PadState m_pad{};
    Screen m_screen = Screen::Login;
    Pane m_pane = Pane::Guilds;
    std::string m_token;
    std::string m_loginError;
    bool m_quit = false;
    uint64_t m_frameTime = 0;

    // Selection. Guild index 0 is the direct messages list.
    int m_guildIndex = 0;
    int m_guildScroll = 0;
    int m_channelIndex = 0;
    float m_channelScroll = 0;
    Snowflake m_openChannel = 0;
    std::vector<ChannelRow> m_rows;
    uint32_t m_rowsVersion = ~0u;
    int m_rowsGuild = -1;

    // Messages pane.
    float m_msgScroll = 0;  // distance from the bottom, in logical px
    std::vector<MessageLayout> m_layout;
    float m_layoutHeight = 0;
    uint32_t m_layoutVersion = ~0u;
    Snowflake m_layoutChannel = 0;

    // Input repeat and touch.
    uint64_t m_repeatAt = 0;
    u64 m_repeatKey = 0;
    bool m_touching = false;
    float m_touchStartX = 0, m_touchStartY = 0, m_touchLastY = 0;
    bool m_touchDragged = false;

    // Mouse mode (Switch 2 Joy-Con optical pointer).
#if defined(__SWITCH2__)
    PadPointer m_pointer{};
    bool m_pointerReady = false;
#endif
    bool m_mouseMode = false;       // engaged: cursor shown, pointer drives the UI
    int m_mouseSide = 0;            // PadPointerSide of the mouse Joy-Con
    float m_mouseX = 640, m_mouseY = 360;
    float m_mouseTravel = 0;        // movement while not engaged
    uint64_t m_mouseMovedAt = 0;    // last movement while not engaged
    uint64_t m_mouseActiveAt = 0;   // last activity while engaged
    bool m_mouseDown = false, m_mouseDragged = false;
    float m_mouseStartX = 0, m_mouseStartY = 0, m_mouseLastY = 0;
    uint64_t m_logoutArmedUntil = 0;

    void startClient();
    void promptToken();
    bool promptText(const char *header, const char *guide, std::string &out, bool token);
    void composeMessage();

    void handleInput();
    void handleTouch();
    // Updates mouse mode. Returns pad buttons to ignore this frame (the mouse
    // Joy-Con's shoulders and stick, which act as its buttons and wheel).
    u64 handlePointer();
    // Shared by touch and mouse.
    void tapAt(float x, float y);
    void dragScroll(float start_x, float dy);
    void wheelScroll(float x, float y, float amount);
    int hitGuild(float x, float y) const;       // guild bar index, or -1
    int hitChannelRow(float x, float y) const;  // row index, or -1
    Rect loginButtonRect() const;
    Rect jumpPillRect() const;
    bool hovering(Rect r) const;
    void drawCursor();
    void moveSelection(int delta);
    void activate();
    void back();
    void openChannel(Snowflake id);

    void rebuildRows();
    Snowflake currentGuildId() const;
    void rebuildLayout(float width);

    void drawLogin();
    void drawMain();
    void drawGuildBar(Rect area);
    void drawChannelList(Rect area);
    void drawUserPanel(Rect area);
    void drawMessages(Rect area);
    void drawHints(Rect area);
    // Draws the image at icon (a CDN path) as a circle, or a coloured circle
    // with the name's first letter while it loads or when there is none.
    void drawAvatar(float cx, float cy, float radius, const std::string &name, Snowflake id,
                    const std::string &icon = {});
};
