// SPDX-License-Identifier: MIT
#include "ui/app.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
    m_remote.stop();
    m_remoteStarted = false;
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

bool App::promptText(const char *header, const char *guide, std::string &out, Kbd kind,
                     int max_len)
{
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0)))
        return false;
    if (kind == Kbd::Password)
        swkbdConfigMakePresetPassword(&kbd);
    else
        swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, header);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetInitialText(&kbd, out.c_str());
    switch (kind) {
    case Kbd::Message:
        swkbdConfigSetStringLenMax(&kbd, max_len ? max_len : 2000);
        swkbdConfigSetReturnButtonFlag(&kbd, 1);  // allow newlines
        swkbdConfigSetTextDrawType(&kbd, SwkbdTextDrawType_Box);
        break;
    case Kbd::Line:
        swkbdConfigSetType(&kbd, SwkbdType_QWERTY);
        swkbdConfigSetStringLenMax(&kbd, max_len ? max_len : 200);
        swkbdConfigSetDicFlag(&kbd, 0);
        break;
    case Kbd::Password:
        swkbdConfigSetStringLenMax(&kbd, max_len ? max_len : 128);
        break;
    case Kbd::Digits:
        swkbdConfigSetType(&kbd, SwkbdType_NumPad);
        swkbdConfigSetStringLenMax(&kbd, max_len ? max_len : 8);
        break;
    }
    std::vector<char> buf(8192, 0);
    Result rc = swkbdShow(&kbd, buf.data(), buf.size());
    swkbdClose(&kbd);
    if (R_FAILED(rc)) {
        memset(buf.data(), 0, buf.size());
        return false;
    }
    out = buf.data();
    memset(buf.data(), 0, buf.size());  // may hold a password
    return true;
}

// --- Login -------------------------------------------------------------------

namespace {
// QR-code login (discord/remoteauth). Off for now: Discord answers the
// ticket exchange with a captcha, which we can't show. Worth trying again
// once the client identity (identity.json) carries current build numbers.
constexpr bool kQrLogin = false;

constexpr float kLoginFormW = 480;  // left column; the QR panel takes the right
constexpr float kLoginCardW = kQrLogin ? 920 : kLoginFormW + 96, kLoginCardH = 480;
constexpr float kLoginRowH = 52, kLoginRowGap = 12;

Rect loginCard()
{
    return {Renderer::kWidth / 2 - kLoginCardW / 2, Renderer::kHeight / 2 - kLoginCardH / 2,
            kLoginCardW, kLoginCardH};
}

// Keeps characters a code may contain; drops spaces and dashes.
std::string cleanCode(const std::string &in)
{
    std::string out;
    for (char c : in)
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            out.push_back((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    return out;
}

void wipe(std::string &s)
{
    s.assign(s.size(), '\0');
    s.clear();
}
}  // namespace

std::vector<App::LoginItem> App::loginItems() const
{
    std::vector<LoginItem> items;
    bool busy = m_auth.busy();
    if (m_loginStep == LoginStep::Credentials) {
        LoginItem name{LoginAction::EditLogin, {}, "EMAIL OR PHONE NUMBER", m_loginName, true};
        LoginItem pass{LoginAction::EditPassword, {}, "PASSWORD", std::string(m_loginPassword.size(), '*'), true};
        LoginItem go{LoginAction::Submit, {}, busy ? "Logging in..." : "Log in", {}, false, true};
        go.enabled = !busy && !m_loginName.empty() && !m_loginPassword.empty();
        items = {name, pass, go};
    } else {
        if (m_mfaTotp)
            items.push_back({LoginAction::Totp, {}, "Use a code from your authenticator app", {}, false, true});
        if (m_mfaSms)
            items.push_back({LoginAction::Sms, {}, m_smsSent ? "Enter the code we texted you" : "Text me a code", {},
                             false, true});
        if (m_mfaBackup)
            items.push_back({LoginAction::Backup, {}, "Use a backup code", {}, false, true});
        items.push_back({LoginAction::Back, {}, "Back", {}, false, false});
        for (auto &it : items)
            if (it.action != LoginAction::Back)
                it.enabled = !busy;
    }

    Rect card = loginCard();
    float y = card.y + 124;
    for (auto &it : items) {
        float h = it.field ? kLoginRowH + 20 : kLoginRowH - 6;
        it.rect = {card.x + 48, y + (it.field ? 20 : 0), kLoginFormW, h - (it.field ? 20 : 0)};
        y += h + kLoginRowGap;
    }
    return items;
}

void App::handleLoginInput(u64 down)
{
    auto items = loginItems();
    int n = (int)items.size();
    m_loginFocus = std::clamp(m_loginFocus, 0, n - 1);
    if (down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown)) {
        int dir = (down & HidNpadButton_AnyUp) ? -1 : 1;
        m_loginFocus = std::clamp(m_loginFocus + dir, 0, n - 1);
    }
    if (kQrLogin && (down & HidNpadButton_Y))
        m_remote.refresh();
    if (down & HidNpadButton_A)
        loginActivate(items[m_loginFocus].action);
    else if ((down & HidNpadButton_B) && m_loginStep == LoginStep::Mfa)
        loginActivate(LoginAction::Back);
}

void App::loginActivate(LoginAction action)
{
    if (m_auth.busy() && action != LoginAction::Back)
        return;
    switch (action) {
    case LoginAction::EditLogin:
        if (promptText("Email or phone number", "The email or phone number of your Discord account",
                       m_loginName, Kbd::Line, 320)) {
            while (!m_loginName.empty() && m_loginName.back() == ' ')
                m_loginName.pop_back();
            while (!m_loginName.empty() && m_loginName.front() == ' ')
                m_loginName.erase(0, 1);
            if (!m_loginName.empty())
                m_loginFocus = m_loginPassword.empty() ? 1 : 2;
        }
        break;
    case LoginAction::EditPassword: {
        std::string pw = m_loginPassword;
        if (promptText("Password", "Your Discord password", pw, Kbd::Password)) {
            wipe(m_loginPassword);
            m_loginPassword = std::move(pw);
            if (!m_loginPassword.empty())
                m_loginFocus = m_loginName.empty() ? 0 : 2;
        }
        wipe(pw);
        break;
    }
    case LoginAction::Submit:
        submitLogin();
        break;
    case LoginAction::Totp: {
        std::string code;
        if (promptText("Authenticator code", "The 6-digit code from your authenticator app", code, Kbd::Digits, 6)) {
            code = cleanCode(code);
            if (!code.empty()) {
                m_loginError.clear();
                m_loginInfo = "Checking the code...";
                m_auth.submitMfa("totp", code, [this](const discord::AuthResult &r) { onAuthResult(r); });
            }
        }
        break;
    }
    case LoginAction::Sms:
        if (!m_smsSent) {
            m_loginError.clear();
            m_loginInfo = "Sending a text message...";
            m_auth.sendSms([this](const discord::AuthResult &r) { onAuthResult(r); });
        } else {
            std::string code;
            if (promptText("Text message code", "The 6-digit code we texted you", code, Kbd::Digits, 6)) {
                code = cleanCode(code);
                if (!code.empty()) {
                    m_loginError.clear();
                    m_loginInfo = "Checking the code...";
                    m_auth.submitMfa("sms", code, [this](const discord::AuthResult &r) { onAuthResult(r); });
                }
            }
        }
        break;
    case LoginAction::Backup: {
        std::string code;
        if (promptText("Backup code", "One of your 8-character backup codes", code, Kbd::Line, 16)) {
            code = cleanCode(code);
            if (!code.empty()) {
                m_loginError.clear();
                m_loginInfo = "Checking the code...";
                m_auth.submitMfa("backup", code, [this](const discord::AuthResult &r) { onAuthResult(r); });
            }
        }
        break;
    }
    case LoginAction::Back:
        m_auth.reset();
        m_loginStep = LoginStep::Credentials;
        m_loginFocus = 1;
        m_loginError.clear();
        m_loginInfo.clear();
        m_smsSent = false;
        break;
    }
}

void App::submitLogin()
{
    if (m_auth.busy() || m_loginName.empty() || m_loginPassword.empty())
        return;
    m_loginError.clear();
    m_loginInfo.clear();
    m_auth.login(m_loginName, m_loginPassword, [this](const discord::AuthResult &r) { onAuthResult(r); });
}

void App::onAuthResult(const discord::AuthResult &r)
{
    using Kind = discord::AuthResult::Kind;
    m_loginInfo.clear();
    switch (r.kind) {
    case Kind::Token:
        finishLogin(r.token);
        break;
    case Kind::Mfa:
        // The password is no longer needed.
        wipe(m_loginPassword);
        m_loginStep = LoginStep::Mfa;
        m_loginFocus = 0;
        m_loginError.clear();
        m_mfaTotp = r.mfa_totp;
        m_mfaSms = r.mfa_sms;
        m_mfaBackup = r.mfa_backup;
        m_mfaWebauthn = r.mfa_webauthn;
        m_smsSent = false;
        if (!m_mfaTotp && !m_mfaSms && !m_mfaBackup)
            m_loginError = "This account only allows security keys, which OunceCord can't use.";
        break;
    case Kind::SmsSent:
        m_smsSent = true;
        m_loginInfo = r.phone.empty() ? "Code sent." : "Code sent to " + r.phone + ".";
        break;
    case Kind::Captcha:
        m_loginError = "Discord asked for a captcha, which can't be shown here. "
                       "Try again later, or check your email for a login alert.";
        break;
    case Kind::Error:
        m_loginError = r.message;
        if (r.ticket_expired && m_loginStep == LoginStep::Mfa) {
            m_loginStep = LoginStep::Credentials;
            m_loginFocus = 1;
            m_smsSent = false;
        }
        break;
    }
}

void App::finishLogin(const std::string &token)
{
    wipe(m_loginPassword);
    m_loginStep = LoginStep::Credentials;
    m_loginFocus = 0;
    m_loginError.clear();
    m_loginInfo.clear();
    m_smsSent = false;
    m_token = token;
    if (!config::saveToken(token))
        logf("app: could not save the session token");
    logf("app: logged in");
    startClient();
}

void App::logOut(const std::string &message)
{
    m_client.stop();
    config::clearToken();
    m_token.clear();
    m_screen = Screen::Login;
    m_loginStep = LoginStep::Credentials;
    m_loginFocus = m_loginName.empty() ? 0 : 1;
    m_loginError = message;
    m_loginInfo.clear();
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
    if (!promptText(header.c_str(), "Type a message", text, Kbd::Message))
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
        if (kQrLogin && qrPanelRect().contains(x, y)) {
            m_remote.refresh();
            return;
        }
        auto items = loginItems();
        for (size_t i = 0; i < items.size(); i++) {
            if (items[i].rect.contains(x, y)) {
                m_loginFocus = (int)i;
                if (items[i].enabled || items[i].field)
                    loginActivate(items[i].action);
                break;
            }
        }
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
        handleLoginInput(down);
        return;
    }

    // A rejected session sends us back to the login screen.
    if (m_client.status() == discord::GatewayStatus::AuthFailed) {
        logOut("Your session has ended. Log in again.");
        return;
    }

    if (down & HidNpadButton_Minus) {
        if (m_frameTime < m_logoutArmedUntil) {
            // Ends the session on Discord's side too, so the token is useless.
            m_auth.logout(m_token);
            logOut("Logged out.");
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
    m_auth.update();
    if (kQrLogin && m_screen == Screen::Login) {
        if (!m_remoteStarted) {
            m_remote.start();
            m_remoteStarted = true;
        }
        std::string token;
        if (m_remote.takeToken(token)) {
            logf("app: logged in with a QR code");
            finishLogin(token);
        }
    }
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
    Rect card = loginCard();
    m_r.fillRoundRect(card, 10, col::bg_channels);

    const float formCx = card.x + 48 + kLoginFormW / 2;
    float y = card.y + 30;
    const char *title = m_loginStep == LoginStep::Mfa ? "Two-factor authentication" : "Welcome back!";
    m_r.drawText(formCx - m_r.measureText(title, 28) / 2, y, title, 28, col::bright);
    y += 46;
    const char *sub = m_loginStep == LoginStep::Mfa ? "Confirm it's you to finish logging in."
                                                    : "Log in to Discord with OunceCord.";
    m_r.drawText(formCx - m_r.measureText(sub, kTextSize) / 2, y, sub, kTextSize, col::muted);
    if (kQrLogin)
        drawQrPanel(qrPanelRect());

    auto items = loginItems();
    m_loginFocus = std::clamp(m_loginFocus, 0, (int)items.size() - 1);
    float bottom = y;
    for (size_t i = 0; i < items.size(); i++) {
        const LoginItem &it = items[i];
        bool focused = (int)i == m_loginFocus && !m_mouseMode;
        bool hover = hovering(it.rect);
        Rect r = it.rect;
        if (it.field) {
            m_r.drawText(r.x, r.y - 20, it.label, 12, col::muted);
            if (focused)
                m_r.fillRoundRect({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 6, col::accent);
            m_r.fillRoundRect(r, 4, hover ? col::hover : col::bg_guilds);
            std::string value = it.value.empty() ? "Press (A) to type" : it.value;
            value = m_r.ellipsize(value, kTextSize, r.w - 28);
            m_r.drawText(r.x + 14, r.y + r.h / 2 - 11, value, kTextSize, it.value.empty() ? col::faint : col::text);
        } else {
            Color bg = it.primary ? (hover || focused ? col::accent_hover : col::accent)
                                  : (hover || focused ? col::selected : col::bg_input);
            if (!it.enabled)
                bg = Color(bg.r, bg.g, bg.b, 110);
            if (focused && it.primary)
                m_r.fillRoundRect({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, 6, col::bright);
            m_r.fillRoundRect(r, 4, bg);
            m_r.drawText(r.x + r.w / 2 - m_r.measureText(it.label, 17) / 2, r.y + r.h / 2 - 11, it.label, 17,
                         it.enabled ? col::bright : col::muted);
        }
        bottom = r.y + r.h;
    }

    // Status line(s) under the form.
    y = bottom + 16;
    const std::string &msg = !m_loginError.empty() ? m_loginError : m_loginInfo;
    if (!msg.empty()) {
        Color c = !m_loginError.empty() ? col::danger : col::muted;
        for (const auto &line : m_r.wrapText(msg, kSmallSize, kLoginFormW)) {
            m_r.drawText(formCx - m_r.measureText(line, kSmallSize) / 2, y, line, kSmallSize, c);
            y += 19;
        }
    }

    y = card.y + card.h - 44;
    const char *warn1 = "Third-party clients break Discord's Terms of Service.";
    const char *warn2 = "Your account could be banned. Your session is saved on the SD card.";
    m_r.drawText(W / 2 - m_r.measureText(warn1, 13) / 2, y, warn1, 13, col::faint);
    m_r.drawText(W / 2 - m_r.measureText(warn2, 13) / 2, y + 18, warn2, 13, col::faint);

    discord::RemoteAuth::Phase qp = m_remote.snapshot().phase;
    bool stale = qp == discord::RemoteAuth::Phase::Expired || qp == discord::RemoteAuth::Phase::Failed;
    std::string hint = m_mouseMode ? "Click to select    (+) Exit"
                       : m_loginStep == LoginStep::Mfa ? "(A) Select    (B) Back    (+) Exit"
                                                       : "(A) Select    (+) Exit";
    if (kQrLogin && stale && !m_mouseMode)
        hint = "(Y) New QR code    " + hint;
    m_r.drawText(W - 24 - m_r.measureText(hint, kSmallSize), H - 32, hint, kSmallSize, col::muted);
}

Rect App::qrPanelRect() const
{
    Rect card = loginCard();
    float x = card.x + 48 + kLoginFormW + 40;
    return {x, card.y + 30, card.x + card.w - 40 - x, 340};
}

void App::drawQrPanel(Rect area)
{
    using Phase = discord::RemoteAuth::Phase;
    discord::RemoteAuth::Snapshot snap = m_remote.snapshot();
    const float cx = area.x + area.w / 2;
    const float box = 220;
    Rect qr_box{cx - box / 2, area.y + 6, box, box};
    float text_y = qr_box.y + box + 22;

    auto centered = [&](const std::string &text, float size, Color c) {
        for (const auto &line : m_r.wrapText(text, size, area.w)) {
            m_r.drawText(cx - m_r.measureText(line, size) / 2, text_y, line, size, c);
            text_y += size + 6;
        }
    };

    if (snap.phase == Phase::Scanned || snap.phase == Phase::LoggingIn) {
        m_r.fillRoundRect(qr_box, 8, col::bg_guilds);
        const User &u = snap.user;
        std::string name = u.displayName().empty() ? std::string("your account") : u.displayName();
        drawAvatar(cx, qr_box.y + box / 2 - 20, 44, name, u.id, u.id ? discord::cdn::avatarPath(u, 128) : std::string());
        std::string who = m_r.ellipsize(name, kTextSize, box - 20);
        m_r.drawText(cx - m_r.measureText(who, kTextSize) / 2, qr_box.y + box / 2 + 38, who, kTextSize, col::bright);
        centered("Check your phone!", 22, col::bright);
        centered(snap.phase == Phase::LoggingIn ? "Logging in..." : "Confirm the login in the Discord app.",
                 kSmallSize, col::muted);
        return;
    }

    bool ready = snap.phase == Phase::WaitingScan && !snap.url.empty();
    if (ready && snap.url_version != m_qrVersion) {
        m_qr = qr::encode(snap.url);
        m_qrVersion = snap.url_version;
    }
    if (ready && m_qr.size > 0) {
        // White background with a quiet zone of four modules.
        m_r.fillRoundRect(qr_box, 8, Color(255, 255, 255));
        float module = std::floor((box - 16) / (m_qr.size + 8));
        float side = module * m_qr.size;
        float ox = std::floor(cx - side / 2), oy = std::floor(qr_box.y + box / 2 - side / 2);
        for (int y = 0; y < m_qr.size; y++) {
            // Merge horizontal runs into one rectangle each.
            int x = 0;
            while (x < m_qr.size) {
                if (!m_qr.at(x, y)) {
                    x++;
                    continue;
                }
                int start = x;
                while (x < m_qr.size && m_qr.at(x, y))
                    x++;
                m_r.fillRect({ox + start * module, oy + y * module, (x - start) * module, module}, Color(0, 0, 0));
            }
        }
        centered("Log in with QR code", 22, col::bright);
        centered("Scan this with the Discord mobile app to log in instantly.", kSmallSize, col::muted);
        return;
    }

    m_r.fillRoundRect(qr_box, 8, hovering(qr_box) ? col::hover : col::bg_guilds);
    std::string inside;
    if (snap.phase == Phase::Expired)
        inside = "QR code expired";
    else if (snap.phase == Phase::Failed)
        inside = "QR login unavailable";
    else
        inside = "Making a QR code...";
    m_r.drawText(cx - m_r.measureText(inside, kTextSize) / 2, qr_box.y + box / 2 - 10, inside, kTextSize, col::muted);
    centered("Log in with QR code", 22, col::bright);
    if (snap.phase == Phase::Expired)
        centered(m_mouseMode ? "Click the box for a new code." : "Press (Y) for a new code.", kSmallSize, col::muted);
    else if (snap.phase == Phase::Failed)
        centered(snap.message + (m_mouseMode ? ". Click to retry." : ". Press (Y) to retry."), kSmallSize, col::muted);
    else
        centered("Scan it with the Discord mobile app to log in instantly.", kSmallSize, col::muted);
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
