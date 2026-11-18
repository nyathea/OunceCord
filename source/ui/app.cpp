// SPDX-License-Identifier: MIT
#include "ui/app.h"

#include <algorithm>
#include <cmath>

#include "config.h"
#include "discord/cdn.h"
#include "discord/rest.h"
#include "util/util.h"

namespace {

namespace col {
constexpr Color bg_guilds = Color::hex(0x1b1c20);
constexpr Color bg_channels = Color::hex(0x26282d);
constexpr Color bg_messages = Color::hex(0x303238);
constexpr Color bg_panel = Color::hex(0x202226);
constexpr Color bg_input = Color::hex(0x3a3d44);
constexpr Color selected = Color::hex(0x41444b);
constexpr Color hover = Color::hex(0x373a40);
constexpr Color accent = Color::hex(0x5b6af0);
constexpr Color accent_hover = Color::hex(0x4752c4);
constexpr Color text = Color::hex(0xdcdee3);
constexpr Color bright = Color::hex(0xf4f5f7);
constexpr Color muted = Color::hex(0x959ba5);
constexpr Color faint = Color::hex(0x6d727c);
constexpr Color danger = Color::hex(0xf0484b);
constexpr Color online = Color::hex(0x2fb164);
constexpr Color warn = Color::hex(0xf0b232);
constexpr Color link = Color::hex(0x00a8fc);
}  // namespace col

constexpr float kGuildBarW = 76;
constexpr float kChannelW = 248;
constexpr float kRowH = 34;
constexpr float kHeaderH = 48;
constexpr float kHintsH = 30;
constexpr float kInputH = 52;
constexpr float kUserPanelH = 56;

constexpr float kTextSize = 17;
constexpr float kSmallSize = 14;

// Mouse mode: optical travel (logical px) that engages it, idle time that
// ends it, and logical px scrolled per wheel step of the Joy-Con stick.
constexpr float kMouseEngageTravel = 24;
constexpr uint64_t kMouseIdleMs = 5000;
constexpr float kMouseWheelStep = 6;

constexpr u64 kRepeatKeys = HidNpadButton_AnyUp | HidNpadButton_AnyDown;

// Messages from the same author within this many ms are grouped.
constexpr uint64_t kGroupWindowMs = 7 * 60 * 1000;

Color avatarColor(Snowflake id)
{
    static const uint32_t palette[] = {0x5b6af0, 0x3ba55c, 0xed4245, 0xfaa61a, 0xeb459e, 0x747f8d, 0x1abc9c, 0x9b59b6};
    return Color::hex(palette[(id >> 22) % (sizeof(palette) / sizeof(palette[0]))]);
}

std::string initials(const std::string &name)
{
    // First letter of up to three words, as the official client does for icons.
    std::string out;
    bool word_start = true;
    int letters = 0;
    size_t i = 0;
    while (i < name.size() && letters < 3) {
        size_t start = i;
        uint32_t cp = utf8Next(name, i);
        if (cp == ' ') {
            word_start = true;
            continue;
        }
        if (word_start) {
            out.append(name, start, i - start);
            word_start = false;
            letters++;
        }
    }
    return out.empty() ? "?" : out;
}

}  // namespace

bool App::init()
{
    if (!m_r.init())
        return false;
    m_images.init();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
#if defined(__SWITCH2__)
    // The pointer area matches the UI's logical space.
    Result prc = padPointerInitialize(&m_pointer, (s32)Renderer::kWidth, (s32)Renderer::kHeight);
    m_pointerReady = R_SUCCEEDED(prc);
    if (!m_pointerReady)
        logf("app: padPointerInitialize failed 0x%x; mouse mode unavailable", prc);
#endif

    m_client.onTokenRefresh = [this](const std::string &token) {
        m_token = token;
        config::saveToken(token);
        logf("app: token refreshed and saved");
    };

    m_token = config::loadToken();
    if (!m_token.empty())
        startClient();
    return true;
}

void App::exit()
{
    m_client.stop();
#if defined(__SWITCH2__)
    if (m_pointerReady)
        padPointerExit(&m_pointer);
    m_pointerReady = false;
#endif
    m_images.exit();
    m_r.exit();
}

void App::startClient()
{
    m_loginError.clear();
    m_screen = Screen::Main;
    m_pane = Pane::Guilds;
    m_guildIndex = 0;
    m_channelIndex = 0;
    m_openChannel = 0;
    m_rowsVersion = ~0u;
    m_layoutVersion = ~0u;
    m_client.start(m_token);
}

bool App::promptText(const char *header, const char *guide, std::string &out, bool token)
{
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, out.c_str());
    if (token) {
        swkbdConfigSetType(&kbd, SwkbdType_QWERTY);
        swkbdConfigSetStringLenMax(&kbd, 200);
        swkbdConfigSetDicFlag(&kbd, 0);
    } else {
        swkbdConfigSetStringLenMax(&kbd, 2000);
        swkbdConfigSetReturnButtonFlag(&kbd, 1);  // allow newlines
        swkbdConfigSetTextDrawType(&kbd, SwkbdTextDrawType_Box);
    }
    std::vector<char> buf(8192, 0);
    Result rc = swkbdShow(&kbd, buf.data(), buf.size());
    swkbdClose(&kbd);
    if (R_FAILED(rc))
        return false;
    out = buf.data();
    return true;
}

void App::promptToken()
{
    std::string token;
    if (!promptText("Discord token", "Paste or type your account token", token, true))
        return;
    token = config::normalizeToken(token);
    if (token.empty())
        return;
    m_token = token;
    if (!config::saveToken(token))
        logf("app: could not save token");
    startClient();
}

void App::composeMessage()
{
    if (!m_openChannel)
        return;
    discord::Client &c = m_client;
    Channel *ch = c.findChannel(m_openChannel);
    if (!ch || !c.canSend(*ch) || c.sendCooldownMs(*ch) > 0)
        return;
    std::string text;
    std::string header = "Message " + ch->label();
    if (!promptText(header.c_str(), "Type a message", text, false))
        return;
    // Trim trailing whitespace; skip empty messages.
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n'))
        text.pop_back();
    if (text.empty())
        return;
    c.sendMessage(m_openChannel, text);
    m_msgScroll = 0;
}

// --- Lists -----------------------------------------------------------------

Snowflake App::currentGuildId() const
{
    if (m_guildIndex <= 0)
        return 0;
    auto &guilds = const_cast<discord::Client &>(m_client).guilds();
    if ((size_t)(m_guildIndex - 1) >= guilds.size())
        return 0;
    return guilds[m_guildIndex - 1].id;
}

void App::rebuildRows()
{
    uint32_t version = m_client.structureVersion();
    if (version == m_rowsVersion && m_rowsGuild == m_guildIndex)
        return;
    Snowflake selected_id = (m_channelIndex >= 0 && (size_t)m_channelIndex < m_rows.size())
                                ? m_rows[m_channelIndex].id
                                : 0;
    bool guild_changed = m_rowsGuild != m_guildIndex;
    m_rowsVersion = version;
    m_rowsGuild = m_guildIndex;
    m_rows.clear();

    if (m_guildIndex == 0) {
        for (auto &c : m_client.dms())
            m_rows.push_back({c.id, c.label(), false, false, discord::cdn::dmIconPath(c)});
    } else if (Guild *g = m_client.findGuild(currentGuildId())) {
        // Categories show only when they hold a channel we can see.
        std::vector<ChannelRow> pending_header;
        for (auto &c : g->channels) {
            if (c.type == ChannelType_GuildCategory) {
                pending_header.clear();
                pending_header.push_back({c.id, c.name, true, false});
                continue;
            }
            if (c.type == ChannelType_PublicThread || c.type == ChannelType_PrivateThread ||
                c.type == ChannelType_AnnouncementThread)
                continue;  // threads are not listed yet
            if (!m_client.canView(c))
                continue;
            if (c.parent_id == 0)
                pending_header.clear();
            for (auto &h : pending_header)
                m_rows.push_back(h);
            pending_header.clear();
            bool voice = c.type == ChannelType_GuildVoice || c.type == ChannelType_GuildStageVoice;
            bool forum = c.type == ChannelType_GuildForum || c.type == ChannelType_GuildMedia;
            ChannelRow row{c.id, c.label(), false, voice || forum};
            if (forum)
                row.label = "= " + c.name;
            m_rows.push_back(row);
        }
    }

    // Keep the selection on the same channel when the list changes.
    m_channelIndex = -1;
    if (!guild_changed && selected_id) {
        for (size_t i = 0; i < m_rows.size(); i++)
            if (m_rows[i].id == selected_id)
                m_channelIndex = (int)i;
    }
    if (m_channelIndex < 0) {
        if (guild_changed)
            m_channelScroll = 0;
        m_channelIndex = 0;
        while ((size_t)m_channelIndex < m_rows.size() && m_rows[m_channelIndex].header)
            m_channelIndex++;
        if ((size_t)m_channelIndex >= m_rows.size())
            m_channelIndex = 0;
    }
}

void App::openChannel(Snowflake id)
{
    Channel *c = m_client.findChannel(id);
    if (!c || !c->isText())
        return;
    m_openChannel = id;
    m_msgScroll = 0;
    m_layoutVersion = ~0u;
    m_client.loadMessages(id);
    m_client.focusChannel(id);
}

// --- Input -----------------------------------------------------------------

void App::moveSelection(int delta)
{
    if (m_pane == Pane::Guilds) {
        int count = (int)m_client.guilds().size() + 1;
        m_guildIndex = std::clamp(m_guildIndex + delta, 0, count - 1);
    } else if (m_pane == Pane::Channels) {
        if (m_rows.empty())
            return;
        int i = m_channelIndex;
        int step = delta > 0 ? 1 : -1;
        for (int n = 0; n < std::abs(delta); n++) {
            int j = i + step;
            while (j >= 0 && (size_t)j < m_rows.size() && m_rows[j].header)
                j += step;
            if (j < 0 || (size_t)j >= m_rows.size())
                break;
            i = j;
        }
        m_channelIndex = i;
    } else {
        m_msgScroll += -delta * 60.0f;
    }
}

void App::activate()
{
    if (m_pane == Pane::Guilds) {
        m_pane = Pane::Channels;
        rebuildRows();
    } else if (m_pane == Pane::Channels) {
        if (m_channelIndex >= 0 && (size_t)m_channelIndex < m_rows.size()) {
            const ChannelRow &row = m_rows[m_channelIndex];
            if (!row.header && !row.voice) {
                openChannel(row.id);
                m_pane = Pane::Messages;
            }
        }
    } else {
        composeMessage();
    }
}

void App::back()
{
    if (m_pane == Pane::Messages)
        m_pane = Pane::Channels;
    else if (m_pane == Pane::Channels)
        m_pane = Pane::Guilds;
}

// --- Pointer input (touch and mouse) ----------------------------------------

constexpr float kGuildItem = 60;

int App::hitGuild(float x, float y) const
{
    if (x >= kGuildBarW || m_screen != Screen::Main)
        return -1;
    float local = y - 8 + m_guildScroll;
    if (local < 0)
        return -1;
    int index = (int)(local / kGuildItem);
    int count = (int)const_cast<discord::Client &>(m_client).guilds().size() + 1;
    return index < count ? index : -1;
}

int App::hitChannelRow(float x, float y) const
{
    if (m_screen != Screen::Main || x < kGuildBarW || x >= kGuildBarW + kChannelW)
        return -1;
    if (y < kHeaderH || y > Renderer::kHeight - kUserPanelH)
        return -1;
    float local = y - kHeaderH - 8 + m_channelScroll;
    if (local < 0)
        return -1;
    int index = (int)(local / kRowH);
    if ((size_t)index >= m_rows.size() || m_rows[index].header)
        return -1;
    return index;
}

Rect App::loginButtonRect() const
{
    const float W = Renderer::kWidth, H = Renderer::kHeight;
    Rect card{W / 2 - 300, H / 2 - 190, 600, 380};
    return {card.x + 60, card.y + 34 + 54 + 48, card.w - 120, 48};
}

Rect App::jumpPillRect() const
{
    float x = kGuildBarW + kChannelW, w = Renderer::kWidth - x;
    float bottom = Renderer::kHeight - kHintsH - kInputH;
    return {x + w / 2 - 80, bottom - 36, 160, 28};
}

bool App::hovering(Rect r) const
{
    return m_mouseMode && r.contains(m_mouseX, m_mouseY);
}

void App::tapAt(float x, float y)
{
    if (m_screen == Screen::Login) {
        if (loginButtonRect().contains(x, y))
            promptToken();
        return;
    }
    if (x < kGuildBarW) {
        int index = hitGuild(x, y);
        if (index >= 0) {
            m_guildIndex = index;
            m_pane = Pane::Channels;
            rebuildRows();
        }
    } else if (x < kGuildBarW + kChannelW) {
        int index = hitChannelRow(x, y);
        if (index >= 0) {
            m_channelIndex = index;
            m_pane = Pane::Channels;
            activate();
        }
    } else {
        m_pane = Pane::Messages;
        if (m_openChannel && m_msgScroll > 200 && jumpPillRect().contains(x, y))
            m_msgScroll = 0;
        else if (y > Renderer::kHeight - kHintsH - kInputH && y < Renderer::kHeight - kHintsH)
            composeMessage();
    }
}

void App::dragScroll(float start_x, float dy)
{
    if (start_x >= kGuildBarW + kChannelW)
        m_msgScroll += dy;
    else if (start_x >= kGuildBarW)
        m_channelScroll = std::max(0.0f, m_channelScroll - dy);
    else
        m_guildScroll = std::max(0, m_guildScroll - (int)dy);
}

void App::wheelScroll(float x, float y, float amount)
{
    // amount > 0 scrolls up (towards older messages / the top of a list).
    if (m_screen != Screen::Main)
        return;
    dragScroll(x, amount);
}

void App::handleTouch()
{
    HidTouchScreenState state{};
    bool down = hidGetTouchScreenStates(&state, 1) && state.count > 0;
    if (down) {
        float x = (float)state.touches[0].x, y = (float)state.touches[0].y;
        m_mouseMode = false;  // touching the screen hands control back
        if (!m_touching) {
            m_touching = true;
            m_touchDragged = false;
            m_touchStartX = x;
            m_touchStartY = y;
            m_touchLastY = y;
        } else {
            float dy = y - m_touchLastY;
            if (std::fabs(y - m_touchStartY) > 12)
                m_touchDragged = true;
            if (m_touchDragged)
                dragScroll(m_touchStartX, dy);
            m_touchLastY = y;
        }
        return;
    }
    if (!m_touching)
        return;
    m_touching = false;
    if (!m_touchDragged)
        tapAt(m_touchStartX, m_touchStartY);
}

u64 App::handlePointer()
{
#if defined(__SWITCH2__)
    if (!m_pointerReady)
        return 0;
    padPointerUpdate(&m_pointer, &m_pad);
    if (!m_pointer.active) {
        m_mouseMode = false;
        return 0;
    }
    float moved = (float)(std::abs(m_pointer.delta_x) + std::abs(m_pointer.delta_y));

    if (!m_mouseMode) {
        // The pointer is "active" whenever a Joy-Con 2 is attached, and its
        // buttons fire during normal play too. Only optical movement means
        // the Joy-Con is on a surface being used as a mouse.
        if (moved > 0) {
            if (m_frameTime - m_mouseMovedAt > 400)
                m_mouseTravel = 0;
            m_mouseTravel += moved;
            m_mouseMovedAt = m_frameTime;
        }
        if (m_mouseTravel < kMouseEngageTravel)
            return 0;
        m_mouseMode = true;
        m_mouseTravel = 0;
        m_mouseActiveAt = m_frameTime;
        m_mouseDown = false;
        logf("app: mouse mode on (%s Joy-Con)", m_pointer.side == PadPointerSide_Left ? "left" : "right");
    }

    m_mouseSide = m_pointer.side;
    m_mouseX = std::clamp((float)m_pointer.x, 0.0f, Renderer::kWidth - 1);
    m_mouseY = std::clamp((float)m_pointer.y, 0.0f, Renderer::kHeight - 1);
    u32 held = padPointerGetButtons(&m_pointer);
    u32 pressed = padPointerGetButtonsDown(&m_pointer);
    u32 released = padPointerGetButtonsUp(&m_pointer);
    if (moved > 0 || m_pointer.wheel_delta_y != 0 || pressed || released || held)
        m_mouseActiveAt = m_frameTime;
    if (m_frameTime - m_mouseActiveAt > kMouseIdleMs) {
        m_mouseMode = false;
        logf("app: mouse mode off (idle)");
        return 0;
    }

    // The stick of the mouse Joy-Con is its scroll wheel (positive = up).
    // Its units are not documented; the raw values are logged (at most once
    // a second) so kMouseWheelStep can be tuned.
    if (m_pointer.wheel_delta_y != 0) {
        static uint64_t s_lastWheelLog;
        if (m_frameTime - s_lastWheelLog > 1000) {
            logf("app: mouse wheel_delta_y=%d", m_pointer.wheel_delta_y);
            s_lastWheelLog = m_frameTime;
        }
        wheelScroll(m_mouseX, m_mouseY, m_pointer.wheel_delta_y * kMouseWheelStep);
    }

    // Left button: click, or hold and drag to scroll.
    if (pressed & HidMouseButton_Left) {
        m_mouseDown = true;
        m_mouseDragged = false;
        m_mouseStartX = m_mouseX;
        m_mouseStartY = m_mouseLastY = m_mouseY;
    }
    if (m_mouseDown && (held & HidMouseButton_Left)) {
        if (std::fabs(m_mouseY - m_mouseStartY) > 12)
            m_mouseDragged = true;
        if (m_mouseDragged)
            dragScroll(m_mouseStartX, m_mouseY - m_mouseLastY);
        m_mouseLastY = m_mouseY;
    }
    if (m_mouseDown && (released & HidMouseButton_Left)) {
        m_mouseDown = false;
        if (!m_mouseDragged)
            tapAt(m_mouseX, m_mouseY);
    }
    if (pressed & HidMouseButton_Right)
        back();

    // Ignore the mouse Joy-Con's own shoulders and stick as pad input; the
    // other Joy-Con keeps working normally.
    if (m_mouseSide == PadPointerSide_Left)
        return HidNpadButton_L | HidNpadButton_ZL | HidNpadButton_StickL | HidNpadButton_StickLLeft |
               HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown;
    return HidNpadButton_R | HidNpadButton_ZR | HidNpadButton_StickR | HidNpadButton_StickRLeft |
           HidNpadButton_StickRRight | HidNpadButton_StickRUp | HidNpadButton_StickRDown;
#else
    return 0;
#endif
}

void App::drawCursor()
{
    if (!m_mouseMode)
        return;
    float x = m_mouseX, y = m_mouseY;
    const Color outline(0, 0, 0, 200), fill(255, 255, 255);
    // Arrow: a triangle body and a short tail, outlined in black.
    m_r.fillTriangle(x - 1.5f, y - 2.5f, x - 1.5f, y + 19.5f, x + 14.5f, y + 13.5f, outline);
    m_r.fillTriangle(x + 3.0f, y + 12.0f, x + 7.5f, y + 22.0f, x + 11.5f, y + 20.0f, outline);
    m_r.fillTriangle(x + 3.0f, y + 12.0f, x + 11.5f, y + 20.0f, x + 7.5f, y + 10.0f, outline);
    m_r.fillTriangle(x, y, x, y + 16.5f, x + 11.5f, y + 12.0f, fill);
    m_r.fillTriangle(x + 4.5f, y + 12.5f, x + 8.0f, y + 20.0f, x + 9.8f, y + 19.0f, fill);
    m_r.fillTriangle(x + 4.5f, y + 12.5f, x + 9.8f, y + 19.0f, x + 6.8f, y + 11.5f, fill);
}

void App::handleInput()
{
    padUpdate(&m_pad);
    u64 down = padGetButtonsDown(&m_pad);
    u64 held = padGetButtons(&m_pad);

    u64 ignore = handlePointer();
    down &= ~ignore;
    held &= ~ignore;
    // D-pad navigation hands control back to the focus-based UI.
    if (m_mouseMode && (down & (HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left | HidNpadButton_Right))) {
        m_mouseMode = false;
        logf("app: mouse mode off (d-pad)");
    }

    if (down & HidNpadButton_Plus) {
        m_quit = true;
        return;
    }

    handleTouch();

    if (m_screen == Screen::Login) {
        if (down & HidNpadButton_A)
            promptToken();
        if (down & HidNpadButton_Y) {
            m_token = config::loadToken();
            if (!m_token.empty())
                startClient();
            else
                m_loginError = std::string("No token found at ") + config::kTokenPath;
        }
        return;
    }

    // Auth failure sends us back to the login screen.
    if (m_client.status() == discord::GatewayStatus::AuthFailed) {
        m_client.stop();
        m_loginError = "Discord rejected that token. Enter a new one.";
        m_screen = Screen::Login;
        return;
    }

    if (down & HidNpadButton_Minus) {
        if (m_frameTime < m_logoutArmedUntil) {
            m_client.stop();
            config::clearToken();
            m_token.clear();
            m_screen = Screen::Login;
            m_loginError = "Logged out. The saved token was removed.";
            return;
        }
        m_logoutArmedUntil = m_frameTime + 3000;
    }

    // Up/down with key repeat.
    u64 dir = held & kRepeatKeys;
    int delta = 0;
    if (dir) {
        if (dir != m_repeatKey) {
            m_repeatKey = dir;
            m_repeatAt = m_frameTime + 350;
            delta = (dir & HidNpadButton_AnyUp) ? -1 : 1;
        } else if (m_frameTime >= m_repeatAt) {
            m_repeatAt = m_frameTime + (m_pane == Pane::Messages ? 30 : 70);
            delta = (dir & HidNpadButton_AnyUp) ? -1 : 1;
        }
    } else {
        m_repeatKey = 0;
    }
    if (delta)
        moveSelection(delta);

    if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
        if (m_pane == Pane::Guilds)
            activate();
        else if (m_pane == Pane::Channels && m_openChannel)
            m_pane = Pane::Messages;
    }
    if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft))
        back();
    if (down & HidNpadButton_A)
        activate();
    if (down & HidNpadButton_B)
        back();
    if (down & HidNpadButton_X)
        composeMessage();
    if (down & HidNpadButton_Y && m_openChannel)
        m_client.reloadMessages(m_openChannel);
    // L/R jump between servers from anywhere.
    if (down & (HidNpadButton_L | HidNpadButton_R)) {
        int count = (int)m_client.guilds().size() + 1;
        m_guildIndex = std::clamp(m_guildIndex + ((down & HidNpadButton_R) ? 1 : -1), 0, count - 1);
        if (m_pane == Pane::Messages)
            m_pane = Pane::Channels;
    }
    // ZL/ZR page through messages; the right stick scrolls smoothly.
    if (m_openChannel) {
        if (down & HidNpadButton_ZL)
            m_msgScroll += 400;
        if (down & HidNpadButton_ZR)
            m_msgScroll -= 400;
        HidAnalogStickState rs = padGetStickPos(&m_pad, 1);
        bool rs_is_wheel = (ignore & HidNpadButton_StickR) != 0;
        if (!rs_is_wheel && std::abs(rs.y) > 6000)
            m_msgScroll += rs.y / 32767.0f * 18.0f;
        if (down & HidNpadButton_StickR)
            m_msgScroll = 0;
    }
}

// --- Frame -----------------------------------------------------------------

bool App::frame()
{
    m_frameTime = nowMs();
    m_client.update();
    m_images.update();
    handleInput();
    if (m_quit)
        return false;

    m_r.beginFrame(col::bg_messages);
    if (m_screen == Screen::Login)
        drawLogin();
    else
        drawMain();
    drawCursor();
    m_r.endFrame();
    return true;
}

void App::drawLogin()
{
    const float W = Renderer::kWidth, H = Renderer::kHeight;
    m_r.fillRect({0, 0, W, H}, col::bg_guilds);
    Rect card{W / 2 - 300, H / 2 - 190, 600, 380};
    m_r.fillRoundRect(card, 10, col::bg_channels);

    float y = card.y + 34;
    const char *title = "OunceCord";
    m_r.drawText(W / 2 - m_r.measureText(title, 34) / 2, y, title, 34, col::bright);
    y += 54;
    const char *sub = "Sign in with your Discord token";
    m_r.drawText(W / 2 - m_r.measureText(sub, kTextSize) / 2, y, sub, kTextSize, col::muted);
    y += 48;

    Rect btn = loginButtonRect();
    m_r.fillRoundRect(btn, 6, hovering(btn) ? col::accent_hover : col::accent);
    const char *label = "(A)  Enter token";
    m_r.drawText(W / 2 - m_r.measureText(label, 18) / 2, btn.y + 13, label, 18, col::bright);
    y += 66;

    std::string file_line = std::string("(Y)  Load from ") + config::kTokenPath;
    m_r.drawText(W / 2 - m_r.measureText(file_line, kSmallSize) / 2, y, file_line, kSmallSize, col::text);
    y += 40;

    if (!m_loginError.empty()) {
        m_r.drawText(W / 2 - m_r.measureText(m_loginError, kSmallSize) / 2, y, m_loginError,
                     kSmallSize, col::danger);
    }
    y = card.y + card.h - 44;
    const char *warn1 = "Third-party clients break Discord's Terms of Service.";
    const char *warn2 = "Your account could be banned. The token is saved on the SD card.";
    m_r.drawText(W / 2 - m_r.measureText(warn1, 13) / 2, y, warn1, 13, col::faint);
    m_r.drawText(W / 2 - m_r.measureText(warn2, 13) / 2, y + 18, warn2, 13, col::faint);

    const char *hint = "(+) Exit";
    m_r.drawText(W - 24 - m_r.measureText(hint, kSmallSize), H - 32, hint, kSmallSize, col::muted);
}

void App::drawMain()
{
    const float H = Renderer::kHeight, W = Renderer::kWidth;
    m_guildIndex = std::clamp(m_guildIndex, 0, (int)m_client.guilds().size());
    rebuildRows();
    drawGuildBar({0, 0, kGuildBarW, H});
    drawChannelList({kGuildBarW, 0, kChannelW, H - kUserPanelH});
    drawUserPanel({kGuildBarW, H - kUserPanelH, kChannelW, kUserPanelH});
    drawMessages({kGuildBarW + kChannelW, 0, W - kGuildBarW - kChannelW, H - kHintsH});
    drawHints({kGuildBarW + kChannelW, H - kHintsH, W - kGuildBarW - kChannelW, kHintsH});

    if (!m_client.ready()) {
        // Connecting overlay.
        Rect box{W / 2 - 220, H / 2 - 60, 440, 120};
        m_r.fillRect({0, 0, W, H}, Color(0, 0, 0, 140));
        m_r.fillRoundRect(box, 10, col::bg_channels);
        const char *msg = m_client.status() == discord::GatewayStatus::Fatal ? "Connection failed"
                                                                               : "Connecting to Discord...";
        m_r.drawText(W / 2 - m_r.measureText(msg, 20) / 2, box.y + 30, msg, 20, col::bright);
        std::string detail = m_client.statusDetail();
        if (!netIsConnected())
            detail = "The console is not connected to the internet.";
        if (!detail.empty()) {
            detail = m_r.ellipsize(detail, kSmallSize, box.w - 40);
            m_r.drawText(W / 2 - m_r.measureText(detail, kSmallSize) / 2, box.y + 70, detail,
                         kSmallSize, col::muted);
        }
    }
}

void App::drawAvatar(float cx, float cy, float radius, const std::string &name, Snowflake id,
                     const std::string &icon)
{
    if (GLuint tex = m_images.get(icon)) {
        m_r.drawImage(tex, {cx - radius, cy - radius, radius * 2, radius * 2}, Shape::Circle);
        return;
    }
    m_r.fillShape({cx - radius, cy - radius, radius * 2, radius * 2}, Shape::Circle, avatarColor(id));
    std::string ini;
    size_t i = 0;
    if (!name.empty()) {
        utf8Next(name, i);
        ini = name.substr(0, i);
    }
    float size = radius * 0.95f;
    float w = m_r.measureText(ini, size);
    m_r.drawText(cx - w / 2, cy - m_r.lineHeight(size) / 2 + 1, ini, size, col::bright);
}

void App::drawGuildBar(Rect area)
{
    m_r.fillRect(area, col::bg_guilds);
    auto &guilds = m_client.guilds();
    int count = (int)guilds.size() + 1;
    const float item = 60;

    // Keep the selection visible.
    float sel_y = 8 + m_guildIndex * item;
    if (sel_y - m_guildScroll < 8)
        m_guildScroll = (int)(sel_y - 8);
    if (sel_y + item - m_guildScroll > area.h - 8)
        m_guildScroll = (int)(sel_y + item - area.h + 8);
    m_guildScroll = std::clamp(m_guildScroll, 0, std::max(0, (int)(8 + count * item - area.h + 16)));

    m_r.pushClip(area);
    for (int i = 0; i < count; i++) {
        float y = 8 + i * item - m_guildScroll;
        if (y + item < 0 || y > area.h)
            continue;
        float cx = area.x + area.w / 2, cy = y + item / 2;
        bool is_sel = i == m_guildIndex;
        bool is_hover = m_mouseMode && hitGuild(m_mouseX, m_mouseY) == i;
        if (is_sel) {
            Color pill = m_pane == Pane::Guilds ? col::bright : col::muted;
            m_r.fillRoundRect({area.x, cy - 20, 4, 40}, 2, pill);
        } else if (is_hover) {
            m_r.fillRoundRect({area.x, cy - 10, 4, 20}, 2, col::muted);
        }
        Color bg = is_sel ? col::accent : is_hover ? col::accent_hover : col::bg_input;
        Rect icon{cx - 24, cy - 24, 48, 48};
        GLuint tex = i == 0 ? 0 : m_images.get(discord::cdn::guildIconPath(guilds[i - 1]));
        if (tex) {
            m_r.drawImage(tex, icon, Shape::Squircle);
            if (i == 0)
                m_r.fillRect({area.x + 18, y + item + 1, area.w - 36, 2}, col::bg_input);
            continue;
        }
        m_r.fillShape(icon, Shape::Squircle, bg);
        std::string label = i == 0 ? "DM" : initials(guilds[i - 1].name);
        float size = label.size() > 3 ? 13 : 16;
        label = m_r.ellipsize(label, size, 44);
        float w = m_r.measureText(label, size);
        m_r.drawText(cx - w / 2, cy - m_r.lineHeight(size) / 2 + 1, label, size, col::bright);
        if (i == 0)
            m_r.fillRect({area.x + 18, y + item + 1, area.w - 36, 2}, col::bg_input);
    }
    m_r.popClip();
}

void App::drawChannelList(Rect area)
{
    m_r.fillRect(area, col::bg_channels);
    // Header: server name.
    std::string title = "Direct Messages";
    if (Guild *g = m_client.findGuild(currentGuildId()))
        title = g->name;
    m_r.fillRect({area.x, area.y, area.w, kHeaderH}, col::bg_channels);
    m_r.drawText(area.x + 16, area.y + 14, m_r.ellipsize(title, 17, area.w - 32), 17, col::bright);
    m_r.fillRect({area.x, area.y + kHeaderH - 1, area.w, 1}, col::bg_guilds);

    Rect list{area.x, area.y + kHeaderH, area.w, area.h - kHeaderH};
    float sel_y = 8 + m_channelIndex * kRowH;
    if (sel_y - m_channelScroll < 8)
        m_channelScroll = sel_y - 8 - (m_channelIndex > 0 && m_rows[m_channelIndex - 1].header ? kRowH : 0);
    if (sel_y + kRowH - m_channelScroll > list.h - 8)
        m_channelScroll = sel_y + kRowH - list.h + 8;
    float max_scroll = std::max(0.0f, 16 + m_rows.size() * kRowH - list.h);
    m_channelScroll = std::clamp(m_channelScroll, 0.0f, max_scroll);

    m_r.pushClip(list);
    if (m_rows.empty()) {
        const char *empty = m_client.ready() ? "Nothing here yet." : "";
        m_r.drawText(list.x + 16, list.y + 16, empty, kSmallSize, col::muted);
    }
    for (size_t i = 0; i < m_rows.size(); i++) {
        float y = list.y + 8 + i * kRowH - m_channelScroll;
        if (y + kRowH < list.y || y > list.y + list.h)
            continue;
        const ChannelRow &row = m_rows[i];
        if (row.header) {
            std::string upper = row.label;
            for (auto &c : upper)
                if (c >= 'a' && c <= 'z')
                    c = c - 'a' + 'A';
            m_r.drawText(list.x + 14, y + 13, m_r.ellipsize(upper, 12, list.w - 28), 12, col::faint);
            continue;
        }
        bool is_sel = (int)i == m_channelIndex;
        bool is_open = row.id == m_openChannel;
        Rect r{list.x + 8, y + 1, list.w - 16, kRowH - 2};
        bool is_hover = m_mouseMode && hitChannelRow(m_mouseX, m_mouseY) == (int)i;
        // In mouse mode the hover replaces the controller focus highlight.
        if (is_sel && m_pane == Pane::Channels && !m_mouseMode)
            m_r.fillRoundRect(r, 4, col::selected);
        else if (is_open)
            m_r.fillRoundRect(r, 4, col::selected);
        else if (is_hover)
            m_r.fillRoundRect(r, 4, col::hover);
        Color tc = row.voice ? col::faint
                   : (is_open || is_hover || (is_sel && !m_mouseMode)) ? col::bright
                                                                       : col::muted;
        float x = r.x + 10;
        if (m_guildIndex == 0) {
            drawAvatar(r.x + 16, r.y + r.h / 2, 12, row.label, row.id, row.icon);
            x = r.x + 36;
        }
        m_r.drawText(x, r.y + 7, m_r.ellipsize(row.label, 16, r.x + r.w - x - 8), 16, tc);
    }
    m_r.popClip();
}

void App::drawUserPanel(Rect area)
{
    m_r.fillRect(area, col::bg_panel);
    const User &me = m_client.me();
    std::string name = m_client.ready() ? me.displayName() : "...";
    drawAvatar(area.x + 28, area.y + area.h / 2, 16, name, me.id,
               m_client.ready() ? discord::cdn::avatarPath(me) : std::string());
    Color dot = col::warn;
    std::string status = "Connecting";
    switch (m_client.status()) {
    case discord::GatewayStatus::Connected: dot = col::online; status = "Online"; break;
    case discord::GatewayStatus::Reconnecting: dot = col::warn; status = "Reconnecting"; break;
    case discord::GatewayStatus::Fatal:
    case discord::GatewayStatus::AuthFailed: dot = col::danger; status = "Disconnected"; break;
    default: break;
    }
    m_r.fillCircle(area.x + 40, area.y + area.h / 2 + 11, 6, col::bg_panel);
    m_r.fillCircle(area.x + 40, area.y + area.h / 2 + 11, 4, dot);
    m_r.drawText(area.x + 54, area.y + 10, m_r.ellipsize(name, 15, area.w - 64), 15, col::bright);
    std::string sub = m_frameTime < m_logoutArmedUntil ? "Press (-) again to log out" : status;
    m_r.drawText(area.x + 54, area.y + 30, sub, 12, m_frameTime < m_logoutArmedUntil ? col::danger : col::muted);
}

void App::rebuildLayout(float width)
{
    auto &cm = m_client.messages(m_openChannel);
    uint32_t version = m_client.messagesVersion();
    if (version == m_layoutVersion && m_layoutChannel == m_openChannel)
        return;
    m_layoutVersion = version;
    m_layoutChannel = m_openChannel;
    m_layout.clear();
    m_layoutHeight = 0;

    const float text_w = width - 72 - 24;
    const float lh = m_r.lineHeight(kTextSize);
    Snowflake prev_author = 0;
    uint64_t prev_ms = 0;
    for (const Message &m : cm.messages) {
        MessageLayout L;
        L.id = m.id;
        uint64_t ms = m.id ? snowflakeUnixMs(m.id) : prev_ms;
        bool system = m.type != 0 && m.type != 19 && m.type != 20 && m.type != 23;
        L.show_header = m.author.id != prev_author || ms - prev_ms > kGroupWindowMs || m.has_reply || system;
        prev_author = m.author.id;
        prev_ms = ms;
        L.author = m.author.displayName();
        L.author_id = m.author.id;
        L.avatar = discord::cdn::avatarPath(m.author);
        L.time = m.id ? formatTime(ms / 1000) : "";
        L.text_color = m.failed ? col::danger : m.pending ? col::faint : col::text;

        std::string content = m.content;
        if (system) {
            switch (m.type) {
            case 7: content = L.author + " joined the server."; break;
            case 6: content = L.author + " pinned a message."; break;
            case 8: content = L.author + " boosted the server."; break;
            case 1: content = L.author + " added someone to the group."; break;
            case 2: content = L.author + " removed someone from the group."; break;
            case 3: content = L.author + " started a call."; break;
            case 4: content = L.author + " changed the channel name."; break;
            case 18: content = L.author + " started a thread: " + m.content; break;
            default: if (content.empty()) content = "(system message)"; break;
            }
            L.text_color = col::muted;
        }
        if (!content.empty())
            L.lines = m_r.wrapText(content, kTextSize, text_w);
        if (m.has_reply) {
            std::string r = (m.reply_author.empty() ? "" : "@" + m.reply_author + " ") + m.reply_content;
            for (auto &c : r)
                if (c == '\n')
                    c = ' ';
            L.reply = m_r.ellipsize(r, 13, text_w);
        }
        for (const auto &a : m.attachments) {
            char size_buf[32];
            snprintf(size_buf, sizeof(size_buf), " (%.1f KB)", a.size / 1024.0);
            L.extras.push_back({"[file] " + a.filename + size_buf, col::link});
        }
        if (m.embed_count) {
            std::string e = m.embed_summary.empty() ? "[embed]" : "[embed] " + m.embed_summary;
            for (auto &c : e)
                if (c == '\n')
                    c = ' ';
            L.extras.push_back({m_r.ellipsize(e, kSmallSize, text_w), col::muted});
        }
        if (m.failed)
            L.extras.push_back({m.error.empty() ? "Failed to send." : "Failed to send: " + m.error, col::danger});

        float h = 0;
        if (L.show_header)
            h += 14 + lh;  // gap + name row
        if (!L.reply.empty())
            h += 20;
        h += L.lines.size() * lh;
        h += L.extras.size() * (kSmallSize * 1.5f);
        h += L.show_header ? 4 : 2;
        if (L.show_header && L.lines.empty() && L.extras.empty())
            h += 4;
        L.height = h;
        m_layoutHeight += h;
        m_layout.push_back(std::move(L));
    }
    m_layoutHeight += 12;
}

void App::drawMessages(Rect area)
{
    m_r.fillRect(area, col::bg_messages);
    Channel *ch = m_openChannel ? m_client.findChannel(m_openChannel) : nullptr;

    // Header.
    m_r.fillRect({area.x, area.y + kHeaderH - 1, area.w, 1}, col::bg_guilds);
    if (!ch) {
        const char *msg = m_client.ready() ? "Pick a channel to start chatting." : "";
        m_r.drawText(area.x + area.w / 2 - m_r.measureText(msg, 18) / 2, area.y + area.h / 2 - 12,
                     msg, 18, col::muted);
        return;
    }
    std::string title = ch->label();
    float tw = m_r.drawText(area.x + 18, area.y + 14, m_r.ellipsize(title, 17, area.w / 2), 17, col::bright);
    if (!ch->topic.empty()) {
        std::string topic = ch->topic;
        for (auto &c : topic)
            if (c == '\n')
                c = ' ';
        m_r.fillRect({area.x + 30 + tw, area.y + 14, 1, 22}, col::bg_input);
        m_r.drawText(area.x + 42 + tw, area.y + 17, m_r.ellipsize(topic, kSmallSize, area.w - tw - 64),
                     kSmallSize, col::muted);
    }

    Rect view{area.x, area.y + kHeaderH, area.w, area.h - kHeaderH - kInputH};
    auto &cm = m_client.messages(m_openChannel);
    rebuildLayout(area.w);

    float max_scroll = std::max(0.0f, m_layoutHeight - view.h);
    m_msgScroll = std::clamp(m_msgScroll, 0.0f, max_scroll + (cm.reached_start ? 0 : 40));
    if (cm.loaded && !cm.loading && !cm.reached_start && m_msgScroll >= max_scroll - 300)
        m_client.loadOlder(m_openChannel);

    m_r.pushClip(view);
    float y = view.y + view.h - m_layoutHeight + m_msgScroll;
    if (m_layoutHeight < view.h)
        y = view.y + view.h - m_layoutHeight;
    if (cm.reached_start && cm.loaded) {
        std::string welcome = "This is the start of " + title + ".";
        m_r.drawText(view.x + 18, y - 34, welcome, kSmallSize, col::faint);
    } else if (cm.loading) {
        m_r.drawText(view.x + 18, y - 30, "Loading...", kSmallSize, col::faint);
    }
    const float lh = m_r.lineHeight(kTextSize);
    const float tx = view.x + 72;
    for (const MessageLayout &L : m_layout) {
        if (y + L.height < view.y || y > view.y + view.h) {
            y += L.height;
            continue;
        }
        float cy = y;
        if (L.show_header) {
            cy += 14;
            if (!L.reply.empty()) {
                m_r.fillRect({view.x + 34, cy + 8, 2, 12}, col::faint);
                m_r.fillRect({view.x + 34, cy + 8, 28, 2}, col::faint);
                m_r.drawText(tx, cy + 1, L.reply, 13, col::muted);
                cy += 20;
            }
            drawAvatar(view.x + 36, cy + 18, 18, L.author, L.author_id, L.avatar);
            float nw = m_r.drawText(tx, cy, m_r.ellipsize(L.author, kTextSize, view.w / 2), kTextSize, col::bright);
            m_r.drawText(tx + nw + 10, cy + 3, L.time, 12, col::faint);
            cy += lh;
        }
        for (const auto &line : L.lines) {
            m_r.drawText(tx, cy, line, kTextSize, L.text_color);
            cy += lh;
        }
        for (const auto &e : L.extras) {
            m_r.drawText(tx, cy + 2, e.first, kSmallSize, e.second);
            cy += kSmallSize * 1.5f;
        }
        y += L.height;
    }
    if (!cm.error.empty())
        m_r.drawText(view.x + 18, view.y + view.h - 30, cm.error, kSmallSize, col::danger);
    m_r.popClip();

    // "Jump to present" when scrolled up.
    if (m_msgScroll > 200) {
        Rect pill = jumpPillRect();
        m_r.fillRoundRect(pill, 14, hovering(pill) ? col::accent_hover : col::accent);
        const char *t = m_mouseMode ? "Jump to latest" : "(RS) Jump to latest";
        m_r.drawText(pill.x + pill.w / 2 - m_r.measureText(t, 13) / 2, pill.y + 6, t, 13, col::bright);
    }

    // Input bar.
    Rect input{area.x + 16, area.y + area.h - kInputH + 4, area.w - 32, kInputH - 14};
    bool can_send = m_client.canSend(*ch);
    m_r.fillRoundRect(input, 8, col::bg_input);
    uint64_t cooldown = can_send ? m_client.sendCooldownMs(*ch) : 0;
    std::string placeholder = !can_send   ? "You can't send messages here."
                              : cooldown ? "Slowmode: wait " + std::to_string(cooldown / 1000 + 1) + "s"
                                         : "(X) Message " + title;
    if (can_send && !cooldown && ch->rate_limit_per_user > 0)
        placeholder += "  (slowmode " + std::to_string(ch->rate_limit_per_user) + "s)";
    m_r.drawText(input.x + 16, input.y + 9, m_r.ellipsize(placeholder, 16, input.w - 32), 16, col::faint);
    std::string typing = m_client.typingText(m_openChannel);
    if (!typing.empty()) {
        typing = m_r.ellipsize(typing, 13, input.w / 2);
        m_r.drawText(input.x + input.w - 14 - m_r.measureText(typing, 13), input.y + 11, typing, 13,
                     col::muted);
    }
}

void App::drawHints(Rect area)
{
    m_r.fillRect(area, col::bg_messages);
    std::string hints;
    if (m_mouseMode) {
        hints = "Mouse mode   (Click) Open   (Stick / hold + drag) Scroll   (D-pad) Back to buttons";
        m_r.drawText(area.x + area.w - 18 - m_r.measureText(hints, 13), area.y + 7, hints, 13, col::faint);
        return;
    }
    switch (m_pane) {
    case Pane::Guilds: hints = "(A) Open   (L/R) Server   (-) Log out   (+) Exit"; break;
    case Pane::Channels: hints = "(A) Open   (B) Back   (L/R) Server   (+) Exit"; break;
    case Pane::Messages: hints = "(X) Write   (Up/Down/RS) Scroll   (ZL/ZR) Page   (Y) Reload   (B) Back"; break;
    }
    m_r.drawText(area.x + area.w - 18 - m_r.measureText(hints, 13), area.y + 7, hints, 13, col::faint);
}
