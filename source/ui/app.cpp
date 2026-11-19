// SPDX-License-Identifier: MIT
#include "ui/app.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "config.h"
#include "settings.h"
#include "discord/cdn.h"
#include "discord/emotes.h"
#include "discord/rest.h"
#include "util/sysinfo.h"
#include "util/util.h"

namespace {

namespace col {
// The current theme's colours (see applyTheme).
Color bg_guilds, bg_channels, bg_messages, bg_panel, bg_input, selected, hover, accent, accent_hover, text, bright,
    muted, faint, danger, online, warn, link;
Color on_accent = Color::hex(0xffffff);  // text on accent/danger fills
Color overlay;                           // a faint wash over rows: lighter on dark themes, darker on light
Color mention_fg, mention_bg;
Color scrim;                             // behind full-screen panels

void applyTheme(Theme t)
{
    // Dark first; the others change what differs.
    bg_guilds = Color::hex(0x1b1c20);
    bg_channels = Color::hex(0x26282d);
    bg_messages = Color::hex(0x303238);
    bg_panel = Color::hex(0x202226);
    bg_input = Color::hex(0x3a3d44);
    selected = Color::hex(0x41444b);
    hover = Color::hex(0x373a40);
    accent = Color::hex(0x5b6af0);
    accent_hover = Color::hex(0x4752c4);
    text = Color::hex(0xdcdee3);
    bright = Color::hex(0xf4f5f7);
    muted = Color::hex(0x959ba5);
    faint = Color::hex(0x6d727c);
    danger = Color::hex(0xf0484b);
    online = Color::hex(0x2fb164);
    warn = Color::hex(0xf0b232);
    link = Color::hex(0x00a8fc);
    overlay = Color(255, 255, 255, 14);
    mention_fg = Color::hex(0xc9cdfb);
    mention_bg = Color(88, 101, 242, 77);
    scrim = Color(17, 18, 20, 245);
    if (t == Theme::Light) {
        bg_guilds = Color::hex(0xe3e5e8);
        bg_channels = Color::hex(0xf2f3f5);
        bg_messages = Color::hex(0xffffff);
        bg_panel = Color::hex(0xebedef);
        bg_input = Color::hex(0xebedef);
        selected = Color::hex(0xd4d7dc);
        hover = Color::hex(0xe3e5e8);
        accent = Color::hex(0x5865f2);
        text = Color::hex(0x313338);
        bright = Color::hex(0x060607);
        muted = Color::hex(0x5c5e66);
        faint = Color::hex(0x80848e);
        danger = Color::hex(0xda373c);
        online = Color::hex(0x23a55a);
        warn = Color::hex(0xd59a00);
        link = Color::hex(0x006ce7);
        overlay = Color(0, 0, 0, 12);
        mention_fg = Color::hex(0x4752c4);
        mention_bg = Color(88, 101, 242, 38);
        scrim = Color(242, 243, 245, 248);
    } else if (t == Theme::Amoled) {
        bg_guilds = Color::hex(0x000000);
        bg_channels = Color::hex(0x000000);
        bg_messages = Color::hex(0x000000);
        bg_panel = Color::hex(0x0b0b0c);
        bg_input = Color::hex(0x1a1b1e);
        selected = Color::hex(0x26272b);
        hover = Color::hex(0x17181a);
        scrim = Color(0, 0, 0, 250);
    }
}
}  // namespace col

constexpr float kGuildBarW = 76;
constexpr float kChannelW = 248;
constexpr float kRowH = 34;
constexpr float kHeaderH = 48;
constexpr float kHintsH = 30;
constexpr float kInputH = 66;  // text box plus the typing line under it
constexpr float kInputBoxH = 40;
// Messages: avatar radius, its centre and the text column, from the pane's left edge.
constexpr float kMsgAvatarR = 22, kMsgAvatarX = 38, kMsgTextX = 74;
constexpr float kUserPanelH = 56;

constexpr float kTextSize = 17;
constexpr float kSmallSize = 14;
// Emote-only messages: emotes are drawn 48 px tall, like Discord's jumbo emoji.
constexpr float kJumboTextSize = 48 / Renderer::kInlineSize;

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
    m_settings = Settings::load();
    col::applyTheme(m_settings.theme);
    if (!m_r.init())
        return false;
    m_images.init();
    m_saver.start();
    m_images.setLocalLoader(
        [this](const std::string &path, std::string &data) { return m_album.loadThumbnail(path, data); });
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
    m_saver.stop();
    m_images.exit();
    m_album.exit();
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

constexpr float kLoginFormW = 480;  // left column; the QR panel takes the right
constexpr float kLoginCardWQr = 920, kLoginCardWPlain = kLoginFormW + 96, kLoginCardH = 480;
constexpr float kLoginRowH = 52, kLoginRowGap = 12;

Rect loginCard(bool qr)
{
    float w = qr ? kLoginCardWQr : kLoginCardWPlain;
    return {Renderer::kWidth / 2 - w / 2, Renderer::kHeight / 2 - kLoginCardH / 2, w, kLoginCardH};
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

    Rect card = loginCard(m_settings.qr_login);
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
    if (m_settings.qr_login && (down & HidNpadButton_Y))
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
    m_viewerOpen = false;
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
    std::string text = m_drafts[m_openChannel].text;
    std::string header = "Message " + ch->label();
    if (!promptText(header.c_str(), "Type a message", text, Kbd::Message))
        return;  // cancelled: keep the draft as it was
    m_drafts[m_openChannel].text = text;
}

App::Draft *App::currentDraft()
{
    auto it = m_openChannel ? m_drafts.find(m_openChannel) : m_drafts.end();
    return it == m_drafts.end() ? nullptr : &it->second;
}

bool App::draftHasContent(const Draft *d) const
{
    return d && (d->has_image || d->text.find_first_not_of(" \n") != std::string::npos);
}

Rect App::attachButtonRect() const
{
    Rect in = inputBoxRect();
    return {in.x + 4, in.y + 4, 32, in.h - 8};
}

Rect App::removeImageRect() const
{
    Rect in = inputBoxRect();
    return {in.x + 42 + 53 + 1, in.y + 9, 22, 22};
}

std::vector<App::ComposeAction> App::composeActions()
{
    std::vector<ComposeAction> a = {ComposeAction::Write, ComposeAction::Attach};
    Draft *d = currentDraft();
    if (d && d->has_image)
        a.push_back(ComposeAction::Remove);
    return a;
}

Rect App::composeMenuRect(size_t count) const
{
    Rect in = inputBoxRect();
    float h = count * 44.0f + 12;
    return {in.x, in.y - 8 - h, 280, h};
}

void App::openComposeMenu()
{
    if (!m_openChannel)
        return;
    Channel *ch = m_client.findChannel(m_openChannel);
    if (!ch || !m_client.canSend(*ch))
        return;
    m_composeMenuOpen = true;
    m_composeMenuIndex = 0;
}

void App::composeMenuChoose(ComposeAction a)
{
    m_composeMenuOpen = false;
    switch (a) {
    case ComposeAction::Write: composeMessage(); break;
    case ComposeAction::Attach: openPicker(); break;
    case ComposeAction::Remove:
        if (Draft *d = currentDraft())
            d->has_image = false;
        break;
    }
}

void App::handleComposeMenuInput(u64 down)
{
    auto actions = composeActions();
    if (down & (HidNpadButton_B | HidNpadButton_X)) {
        m_composeMenuOpen = false;
        return;
    }
    if (down & (HidNpadButton_AnyUp))
        m_composeMenuIndex = std::max(0, m_composeMenuIndex - 1);
    if (down & (HidNpadButton_AnyDown))
        m_composeMenuIndex = std::min((int)actions.size() - 1, m_composeMenuIndex + 1);
    m_composeMenuIndex = std::clamp(m_composeMenuIndex, 0, (int)actions.size() - 1);
    if (down & HidNpadButton_A)
        composeMenuChoose(actions[m_composeMenuIndex]);
}

void App::drawComposeMenu()
{
    auto actions = composeActions();
    Rect r = composeMenuRect(actions.size());
    m_r.fillRoundRect({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, 9, col::bg_guilds);
    m_r.fillRoundRect(r, 8, col::bg_panel);
    Draft *d = currentDraft();
    for (size_t i = 0; i < actions.size(); i++) {
        Rect it{r.x + 6, r.y + 6 + i * 44.0f, r.w - 12, 44};
        bool focus = m_mouseMode ? hovering(it) : (int)i == m_composeMenuIndex;
        if (focus)
            m_r.fillRoundRect(it, 6, actions[i] == ComposeAction::Remove ? col::danger : col::accent);
        const char *label = actions[i] == ComposeAction::Write    ? (d && !d->text.empty() ? "Edit message" : "Write a message")
                            : actions[i] == ComposeAction::Attach ? (d && d->has_image ? "Change picture" : "Attach a picture")
                                                                  : "Remove picture";
        Color fg = focus ? col::on_accent : actions[i] == ComposeAction::Remove ? col::danger : col::text;
        m_r.drawText(it.x + 14, it.y + 12, label, 16, fg);
    }
}

const std::vector<Album::Item> &App::pickerItems() const
{
    return m_pickerTab ? m_album.saved() : m_album.items();
}

Rect App::pickerTabRect(int tab) const
{
    return {Renderer::kWidth - 40 - 260 + tab * 132.0f, 20, 128, 34};
}

void App::setPickerTab(int tab)
{
    if (tab == m_pickerTab)
        return;
    m_pickerTab = tab;
    m_pickerIndex = 0;
    m_pickerScroll = 0;
    if (tab)
        m_album.refreshSaved();
    else
        m_album.refresh();
}

void App::openPicker()
{
    if (m_pickerTab)
        m_album.refreshSaved();
    else
        m_album.refresh();
    m_pickerOpen = true;
    m_pickerIndex = 0;
    m_pickerScroll = 0;
    m_pickerRepeatKey = 0;
}

void App::pickerPick(int index)
{
    const auto &items = pickerItems();
    if (index < 0 || (size_t)index >= items.size() || !m_openChannel)
        return;
    Draft &d = m_drafts[m_openChannel];
    d.has_image = true;
    d.image = items[index];
    m_pickerOpen = false;
}

Rect App::pickerGridArea() const
{
    return {40, 72, Renderer::kWidth - 80, Renderer::kHeight - 72 - 44};
}

Rect App::pickerCellRect(int index) const
{
    Rect g = pickerGridArea();
    const float gap = 12;
    float w = (g.w - gap * (kPickerCols - 1)) / kPickerCols, h = w * 9 / 16;
    float row_h = h + 30;
    int row = index / kPickerCols, col_i = index % kPickerCols;
    return {g.x + col_i * (w + gap), g.y + row * row_h - m_pickerScroll, w, h};
}

int App::hitPicker(float x, float y) const
{
    Rect g = pickerGridArea();
    if (!g.contains(x, y))
        return -1;
    for (size_t i = 0; i < pickerItems().size(); i++) {
        Rect r = pickerCellRect((int)i);
        if (r.y > g.y + g.h)
            break;
        if (r.contains(x, y))
            return (int)i;
    }
    return -1;
}

void App::handlePickerInput(u64 down, u64 held)
{
    if (down & HidNpadButton_B) {
        m_pickerOpen = false;
        return;
    }
    if (down & (HidNpadButton_L | HidNpadButton_R)) {
        setPickerTab((down & HidNpadButton_R) ? 1 : 0);
        return;
    }
    int count = (int)pickerItems().size();
    if (down & HidNpadButton_A) {
        pickerPick(m_pickerIndex);
        return;
    }
    if (!count)
        return;
    // Four directions with key repeat.
    const u64 kDirs = HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
    u64 dir = held & kDirs;
    bool step = false;
    if (dir && dir != m_pickerRepeatKey) {
        m_pickerRepeatKey = dir;
        m_pickerRepeatAt = m_frameTime + 350;
        step = true;
    } else if (dir && m_frameTime >= m_pickerRepeatAt) {
        m_pickerRepeatAt = m_frameTime + 90;
        step = true;
    } else if (!dir) {
        m_pickerRepeatKey = 0;
    }
    int i = m_pickerIndex;
    if (step) {
        if (dir & HidNpadButton_AnyLeft)
            i -= 1;
        else if (dir & HidNpadButton_AnyRight)
            i += 1;
        else if (dir & HidNpadButton_AnyUp)
            i -= kPickerCols;
        else if (dir & HidNpadButton_AnyDown)
            i += kPickerCols;
    }
    if (down & HidNpadButton_ZL)
        i -= kPickerCols * 3;
    if (down & HidNpadButton_ZR)
        i += kPickerCols * 3;
    i = std::clamp(i, 0, count - 1);
    if (i != m_pickerIndex) {
        m_pickerIndex = i;
        m_pickerScrollPending = true;
    }
}

void App::drawPicker()
{
    const float W = Renderer::kWidth, H = Renderer::kHeight;
    m_r.fillRect({0, 0, W, H}, col::scrim);
    m_r.drawText(40, 24, m_pickerTab ? "Attach a saved picture" : "Attach a screenshot", 22, col::bright);
    for (int t = 0; t < 2; t++) {
        Rect tr = pickerTabRect(t);
        bool on = t == m_pickerTab;
        m_r.fillRoundRect(tr, 8, on ? col::accent : hovering(tr) ? col::hover : col::bg_input);
        std::string label = std::string(t == 0 ? "Album" : "Saved");
        if (!m_mouseMode)
            label = (t == 0 ? "(L) " : "(R) ") + label;
        m_r.drawText(tr.x + tr.w / 2 - m_r.measureText(label, 15) / 2, tr.y + 7, label, 15,
                     on ? col::on_accent : col::text);
    }
    const auto &items = pickerItems();
    Rect g = pickerGridArea();
    if (items.empty()) {
        const std::string &msg = m_pickerTab ? m_album.savedError() : m_album.error();
        m_r.drawText(W / 2 - m_r.measureText(msg, 17) / 2, H / 2 - 12, msg, 17, col::muted);
    } else {
        // Clamp the scroll, and keep the focused cell in view.
        Rect first = pickerCellRect(0);
        float row_h = first.h + 30;
        int rows = ((int)items.size() + kPickerCols - 1) / kPickerCols;
        float max_scroll = std::max(0.0f, rows * row_h - g.h);
        if (m_pickerScrollPending) {
            m_pickerScrollPending = false;
            float top = (m_pickerIndex / kPickerCols) * row_h;
            if (top < m_pickerScroll)
                m_pickerScroll = top;
            if (top + row_h > m_pickerScroll + g.h)
                m_pickerScroll = top + row_h - g.h;
        }
        m_pickerScroll = std::clamp(m_pickerScroll, 0.0f, max_scroll);

        m_r.pushClip(g);
        for (size_t i = 0; i < items.size(); i++) {
            Rect r = pickerCellRect((int)i);
            if (r.y + r.h + 30 < g.y)
                continue;
            if (r.y > g.y + g.h)
                break;
            bool focus = m_mouseMode ? hovering(r) : (int)i == m_pickerIndex;
            if (focus)
                m_r.fillRoundRect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, 9, col::accent);
            bool failed = false;
            if (GLuint tex = m_images.get(items[i].key, &failed))
                m_r.drawImage(tex, r, Shape::Rounded);
            else {
                m_r.fillRoundRect(r, 6, col::bg_input);
                const char *t = failed ? "Can't load" : "";
                m_r.drawText(r.x + r.w / 2 - m_r.measureText(t, 13) / 2, r.y + r.h / 2 - 9, t, 13, col::faint);
            }
            std::string label = m_r.ellipsize(items[i].label(), 13, r.w - 4);
            m_r.drawText(r.x + 2, r.y + r.h + 7, label, 13, focus ? col::bright : col::muted);
        }
        m_r.popClip();
    }
    const char *hints = m_mouseMode ? "Click a screenshot to attach it" : "(A) Attach   (L/R) Album / Saved   (ZL/ZR) Page   (B) Cancel";
    m_r.drawText(W - 40 - m_r.measureText(hints, 13), H - 30, hints, 13, col::faint);
}

bool App::canSendNow()
{
    Channel *ch = m_openChannel ? m_client.findChannel(m_openChannel) : nullptr;
    return ch && m_client.canSend(*ch) && m_client.sendCooldownMs(*ch) == 0;
}

void App::sendDraft()
{
    if (!canSendNow())
        return;
    auto it = m_drafts.find(m_openChannel);
    if (it == m_drafts.end() || !draftHasContent(&it->second))
        return;
    std::string text = it->second.text;
    // Trim surrounding whitespace.
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n'))
        text.pop_back();
    size_t start = text.find_first_not_of(" \n");
    text = start == std::string::npos ? std::string() : text.substr(start);
    if (it->second.has_image) {
        discord::Upload up;
        up.filename = it->second.image.uploadName();
        up.content_type = it->second.image.contentType();
        std::string error;
        if (!m_album.loadFile(it->second.image, up.data, error)) {
            // Keep the draft so nothing typed is lost.
            m_composeError = error;
            m_composeErrorUntil = m_frameTime + 5000;
            return;
        }
        m_client.sendMessage(m_openChannel, text, &up);
    } else {
        if (text.empty())
            return;
        m_client.sendMessage(m_openChannel, text);
    }
    m_drafts.erase(it);
    clearMessageSelection();
}

Rect App::inputBoxRect() const
{
    float x = kGuildBarW + kChannelW, w = Renderer::kWidth - x;
    return {x + 16, Renderer::kHeight - kHintsH - kInputH + 4, w - 32, kInputBoxH};
}

Rect App::sendButtonRect() const
{
    Rect in = inputBoxRect();
    return {in.x + in.w - 38, in.y + 4, 32, in.h - 8};
}

int App::selectedIndex() const
{
    if (!m_selActive || m_layout.empty())
        return -1;
    if (m_selHint < m_layout.size() && m_layout[m_selHint].id == m_selId)
        return (int)m_selHint;
    for (size_t i = m_layout.size(); i-- > 0;)
        if (m_layout[i].id == m_selId)
            return (int)i;
    return -1;
}

void App::moveMessageSelection(int delta)
{
    if (m_layout.empty())
        return;
    int i = selectedIndex();
    if (i < 0) {
        if (delta > 0)
            return;  // already following the newest messages
        // Start from the newest message on screen.
        i = (int)m_layout.size() - 1;
        float bottom_from_end = m_msgScroll + 12;  // layout px below the view's bottom
        float end = m_layoutHeight;
        while (i > 0 && end - (m_layout[i].top + m_layout[i].height) < bottom_from_end - 1)
            i--;
        delta += 1;  // the first press only selects
    }
    i += delta;
    if (i >= (int)m_layout.size()) {
        clearMessageSelection();
        return;
    }
    i = std::max(i, 0);
    m_selActive = true;
    m_selId = m_layout[i].id;
    m_selHint = (size_t)i;
    m_selScrollPending = true;
}

void App::clearMessageSelection()
{
    m_selActive = false;
    m_selScrollPending = false;
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
    m_selActive = false;
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
        moveMessageSelection(delta);
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
    } else if (int sel = selectedIndex(); sel >= 0) {
        if (!m_layout[sel].images.empty())
            openViewer((size_t)sel, 0);
        else
            composeMessage();
    } else if (!m_imageHits.empty()) {
        // The newest picture on screen.
        const ImageHit &h = m_imageHits.back();
        openViewer(h.layout, h.image);
    } else {
        composeMessage();
    }
}

void App::back()
{
    if (m_viewerOpen) {
        m_viewerOpen = false;
        return;
    }
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
    if (m_viewerOpen) {
        if (viewerSaveRect().contains(x, y))
            saveViewerImage();
        else
            m_viewerOpen = false;
        return;
    }
    if (m_pickerOpen) {
        for (int t = 0; t < 2; t++)
            if (pickerTabRect(t).contains(x, y)) {
                setPickerTab(t);
                return;
            }
        int i = hitPicker(x, y);
        if (i >= 0)
            pickerPick(i);
        else if (y < pickerGridArea().y)
            m_pickerOpen = false;  // tapping the title bar cancels
        return;
    }
    if (m_settingsOpen) {
        auto items = settingItems();
        for (size_t i = 0; i < items.size(); i++)
            if (items[i].rect.contains(x, y)) {
                m_settingsIndex = (int)i;
                changeSetting(items[i].row, 0);
                return;
            }
        if (!settingsCardRect().contains(x, y))
            m_settingsOpen = false;
        return;
    }
    if (m_composeMenuOpen) {
        auto actions = composeActions();
        Rect r = composeMenuRect(actions.size());
        m_composeMenuOpen = false;
        for (size_t i = 0; i < actions.size(); i++)
            if (Rect{r.x + 6, r.y + 6 + i * 44.0f, r.w - 12, 44}.contains(x, y))
                composeMenuChoose(actions[i]);
        return;
    }
    if (m_screen == Screen::Main) {
        for (const ImageHit &h : m_imageHits) {
            if (!h.rect.contains(x, y))
                continue;
            const ImageBlock &img = m_layout[h.layout].images[h.image];
            if (img.spoiler && !m_revealed.count(img.source))
                m_revealed.insert(img.source);
            else
                openViewer(h.layout, h.image);
            return;
        }
    }
    if (m_screen == Screen::Login) {
        if (m_settings.qr_login && qrPanelRect().contains(x, y)) {
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
    if (settingsGearRect().contains(x, y)) {
        openSettings();
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
        else if (m_openChannel && sendButtonRect().contains(x, y))
            sendDraft();
        else if (m_openChannel && attachButtonRect().contains(x, y))
            openComposeMenu();
        else if (m_openChannel && currentDraft() && currentDraft()->has_image && removeImageRect().contains(x, y))
            currentDraft()->has_image = false;
        else if (m_openChannel && inputBoxRect().contains(x, y))
            composeMessage();
    }
}

void App::dragScroll(float start_x, float dy)
{
    if (m_pickerOpen) {
        m_pickerScroll -= dy;
        return;
    }
    if (start_x >= kGuildBarW + kChannelW) {
        m_msgScroll += dy;
        m_selActive = false;
    }
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
    if (!m_settings.touch) {
        m_touching = false;
        return;
    }
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
    if (!m_settings.mouse) {
        m_mouseMode = false;
        return 0;
    }
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
    if (m_viewerOpen) {
        handleViewerInput(down);
        return;
    }
    if (m_pickerOpen) {
        handlePickerInput(down, held);
        return;
    }
    if (m_composeMenuOpen) {
        handleComposeMenuInput(down);
        return;
    }
    if (m_settingsOpen) {
        handleSettingsInput(down);
        return;
    }

    // A rejected session sends us back to the login screen.
    if (m_client.status() == discord::GatewayStatus::AuthFailed) {
        logOut("Your session has ended. Log in again.");
        return;
    }

    if (down & HidNpadButton_Minus) {
        openSettings();
        return;
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
            m_repeatAt = m_frameTime + (m_pane == Pane::Messages ? 90 : 70);
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
    if (down & HidNpadButton_X && m_pane != Pane::Guilds)
        openComposeMenu();
    if (down & HidNpadButton_Y && m_openChannel)
        sendDraft();
    // L/R jump between servers from anywhere.
    if (down & (HidNpadButton_L | HidNpadButton_R)) {
        int count = (int)m_client.guilds().size() + 1;
        m_guildIndex = std::clamp(m_guildIndex + ((down & HidNpadButton_R) ? 1 : -1), 0, count - 1);
        if (m_pane == Pane::Messages)
            m_pane = Pane::Channels;
    }
    // ZL/ZR page through messages; the right stick moves the highlight,
    // faster the further it's pushed.
    if (m_openChannel) {
        if (down & (HidNpadButton_ZL | HidNpadButton_ZR)) {
            // About a screenful of messages.
            int sel = selectedIndex();
            int from = sel >= 0 ? sel : (int)m_layout.size();
            int dir = (down & HidNpadButton_ZL) ? -1 : 1;
            float covered = 0;
            int n = 0;
            for (int i = from + dir; i >= 0 && i < (int)m_layout.size() && covered < 400; i += dir, n++)
                covered += m_layout[i].height;
            if (dir > 0 && from + n >= (int)m_layout.size() - 1)
                clearMessageSelection();
            else if (n)
                moveMessageSelection(dir * n + (sel < 0 ? dir : 0));
        }
        HidAnalogStickState rs = padGetStickPos(&m_pad, 1);
        bool rs_is_wheel = (ignore & HidNpadButton_StickR) != 0;
        float push = std::abs(rs.y) / 32767.0f;
        if (!rs_is_wheel && m_pane == Pane::Messages && push > 0.25f) {
            if (m_frameTime >= m_stickRepeatAt) {
                moveMessageSelection(rs.y > 0 ? -1 : 1);
                m_stickRepeatAt = m_frameTime + (uint64_t)(260 - 220 * std::min(1.0f, (push - 0.25f) / 0.7f));
            }
        } else {
            m_stickRepeatAt = 0;
        }
        if (down & HidNpadButton_StickR)
            clearMessageSelection();
    }
}

// --- Frame -----------------------------------------------------------------

bool App::frame()
{
    m_frameTime = nowMs();
    m_client.update();
    m_auth.update();
    {
        std::string msg;
        bool ok = true;
        while (m_saver.takeResult(msg, ok))
            toast(msg, ok);
    }
    if (m_settings.qr_login && m_screen == Screen::Login) {
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
    Rect card = loginCard(m_settings.qr_login);
    m_r.fillRoundRect(card, 10, col::bg_channels);

    const float formCx = card.x + 48 + kLoginFormW / 2;
    float y = card.y + 30;
    const char *title = m_loginStep == LoginStep::Mfa ? "Two-factor authentication" : "Welcome back!";
    m_r.drawText(formCx - m_r.measureText(title, 28) / 2, y, title, 28, col::bright);
    y += 46;
    const char *sub = m_loginStep == LoginStep::Mfa ? "Confirm it's you to finish logging in."
                                                    : "Log in to Discord with OunceCord.";
    m_r.drawText(formCx - m_r.measureText(sub, kTextSize) / 2, y, sub, kTextSize, col::muted);
    if (m_settings.qr_login)
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
                         !it.enabled ? col::muted : it.primary ? col::on_accent : col::text);
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
    if (m_settings.qr_login && stale && !m_mouseMode)
        hint = "(Y) New QR code    " + hint;
    m_r.drawText(W - 24 - m_r.measureText(hint, kSmallSize), H - 32, hint, kSmallSize, col::muted);
}

Rect App::qrPanelRect() const
{
    Rect card = loginCard(m_settings.qr_login);
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
    if (m_composeMenuOpen)
        drawComposeMenu();
    if (m_pickerOpen)
        drawPicker();
    if (m_viewerOpen)
        drawViewer();
    if (m_settingsOpen)
        drawSettings();
    drawToast();
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
    m_r.drawText(cx - w / 2, cy - m_r.lineHeight(size) / 2 + 1, ini, size, col::on_accent);
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
        m_r.drawText(cx - w / 2, cy - m_r.lineHeight(size) / 2 + 1, label, size,
                     is_sel || is_hover ? col::on_accent : col::text);
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
    m_r.drawText(area.x + 54, area.y + 10, m_r.ellipsize(name, 15, area.w - 64 - 44), 15, col::bright);
    m_r.drawText(area.x + 54, area.y + 30, status, 12, col::muted);
    // Settings: a gear, with (-) shown for controllers.
    Rect g = settingsGearRect();
    if (hovering(g))
        m_r.fillRoundRect(g, 6, col::hover);
    float gx = g.x + g.w / 2, gy = g.y + g.h / 2;
    Color gc = hovering(g) ? col::bright : col::muted;
    for (int k = 0; k < 8; k++) {  // teeth
        float a = k * 3.14159265f / 4;
        m_r.fillCircle(gx + std::cos(a) * 8.5f, gy + std::sin(a) * 8.5f, 2.6f, gc);
    }
    m_r.fillCircle(gx, gy, 8, gc);
    m_r.fillCircle(gx, gy, 3.5f, col::bg_panel);
    if (!m_mouseMode)
        m_r.drawText(g.x - 24, gy - 8, "(-)", 12, col::faint);
}

// --- Settings ----------------------------------------------------------------

constexpr float kSettingsW = 640, kSettingsRowH = 52, kSettingsHeadH = 34;

Rect App::settingsCardRect() const
{
    return {Renderer::kWidth / 2 - kSettingsW / 2, 36, kSettingsW, Renderer::kHeight - 72};
}

Rect App::settingsGearRect() const
{
    return {kGuildBarW + kChannelW - 44, Renderer::kHeight - kUserPanelH + 12, 32, 32};
}

std::vector<App::SettingItem> App::settingItems() const
{
    Rect c = settingsCardRect();
    float x = c.x + 28, w = c.w - 56, y = c.y + 76;
    std::vector<SettingItem> out;
    auto section = [&] { y += kSettingsHeadH; };
    auto row = [&](SettingRow r, float h = kSettingsRowH) {
        out.push_back({r, {x, y, w, h}});
        y += h + 4;
    };
    section();  // Appearance
    row(SettingRow::Theme);
    y += 10;
    section();  // Input
    row(SettingRow::Mouse);
    row(SettingRow::Touch);
    y += 10;
    section();  // Experimental
    row(SettingRow::QrLogin, 72);
    // Log out sits at the bottom of the card.
    out.push_back({SettingRow::LogOut, {x, c.y + c.h - 28 - 48, w, 48}});
    return out;
}

void App::openSettings()
{
    m_settingsOpen = true;
    m_settingsIndex = 0;
    m_logoutArmedUntil = 0;
    m_composeMenuOpen = false;
}

void App::applySettings()
{
    col::applyTheme(m_settings.theme);
    m_layoutVersion = ~0u;  // message colours are baked into the layout
    if (!m_settings.qr_login && m_remoteStarted) {
        m_remote.stop();
        m_remoteStarted = false;
    }
    if (!m_settings.save())
        toast("Couldn't save settings to the SD card.", false);
}

void App::changeSetting(SettingRow row, int dir)
{
    switch (row) {
    case SettingRow::Theme: {
        int t = (int)m_settings.theme + (dir ? dir : 1);
        m_settings.theme = (Theme)((t % 3 + 3) % 3);
        break;
    }
    case SettingRow::Mouse:
        m_settings.mouse = dir ? dir > 0 : !m_settings.mouse;
        break;
    case SettingRow::Touch:
        m_settings.touch = dir ? dir > 0 : !m_settings.touch;
        break;
    case SettingRow::QrLogin:
        m_settings.qr_login = dir ? dir > 0 : !m_settings.qr_login;
        break;
    case SettingRow::LogOut:
        if (dir)
            return;
        if (m_frameTime < m_logoutArmedUntil) {
            // Ends the session on Discord's side too, so the token is useless.
            m_settingsOpen = false;
            m_logoutArmedUntil = 0;
            m_auth.logout(m_token);
            logOut("Logged out.");
        } else {
            m_logoutArmedUntil = m_frameTime + 3000;
        }
        return;
    }
    applySettings();
}

void App::handleSettingsInput(u64 down)
{
    auto items = settingItems();
    if (down & (HidNpadButton_B | HidNpadButton_Minus)) {
        m_settingsOpen = false;
        return;
    }
    int n = (int)items.size();
    if (down & HidNpadButton_AnyUp)
        m_settingsIndex = (m_settingsIndex + n - 1) % n;
    if (down & HidNpadButton_AnyDown)
        m_settingsIndex = (m_settingsIndex + 1) % n;
    m_settingsIndex = std::clamp(m_settingsIndex, 0, n - 1);
    SettingRow r = items[m_settingsIndex].row;
    if (r != SettingRow::LogOut)
        m_logoutArmedUntil = 0;
    if (down & HidNpadButton_A)
        changeSetting(r, 0);
    else if (down & HidNpadButton_AnyLeft)
        changeSetting(r, -1);
    else if (down & HidNpadButton_AnyRight)
        changeSetting(r, 1);
}

void App::drawSettings()
{
    const float W = Renderer::kWidth, H = Renderer::kHeight;
    m_r.fillRect({0, 0, W, H}, Color(0, 0, 0, 150));
    Rect c = settingsCardRect();
    m_r.fillRoundRect(c, 12, col::bg_channels);
    m_r.drawText(c.x + 28, c.y + 24, "Settings", 24, col::bright);
    const char *close = m_mouseMode ? "Click outside to close" : "(B) Close";
    m_r.drawText(c.x + c.w - 28 - m_r.measureText(close, 13), c.y + 32, close, 13, col::faint);

    auto items = settingItems();
    m_settingsIndex = std::clamp(m_settingsIndex, 0, (int)items.size() - 1);
    auto header = [&](const char *t, float y) { m_r.drawText(c.x + 28, y + 10, t, 12, col::muted); };
    header("APPEARANCE", items[0].rect.y - kSettingsHeadH);
    header("INPUT", items[1].rect.y - kSettingsHeadH);
    header("EXPERIMENTAL", items[3].rect.y - kSettingsHeadH);

    for (size_t i = 0; i < items.size(); i++) {
        const SettingItem &it = items[i];
        Rect r = it.rect;
        bool focus = m_mouseMode ? hovering(r) : (int)i == m_settingsIndex;
        if (it.row == SettingRow::LogOut) {
            bool armed = m_frameTime < m_logoutArmedUntil;
            m_r.fillRoundRect(r, 8, armed || focus ? col::danger : col::bg_input);
            const char *t = armed ? (m_mouseMode ? "Click again to log out" : "Press (A) again to log out") : "Log out";
            m_r.drawText(r.x + r.w / 2 - m_r.measureText(t, 17) / 2, r.y + 13, t, 17,
                         armed || focus ? col::on_accent : col::danger);
            continue;
        }
        m_r.fillRoundRect(r, 8, focus ? col::selected : col::bg_input);
        if (focus && !m_mouseMode)
            m_r.fillRoundRect({r.x, r.y + 8, 3, r.h - 16}, 1.5f, col::accent);
        const char *label = "", *desc = nullptr;
        bool toggle = true, on = false;
        switch (it.row) {
        case SettingRow::Theme: label = "Theme"; toggle = false; break;
        case SettingRow::Mouse:
            label = "Mouse mode";
            desc = "Slide a Joy-Con 2 on a surface to use it as a mouse.";
            on = m_settings.mouse;
            break;
        case SettingRow::Touch: label = "Touch screen"; on = m_settings.touch; break;
        case SettingRow::QrLogin:
            label = "QR code login";
            desc = "Log in by scanning a code with the Discord app. Discord often asks for a captcha here, which "
                   "OunceCord can't show.";
            on = m_settings.qr_login;
            break;
        default: break;
        }
        float ty = desc ? r.y + 8 : r.y + r.h / 2 - 11;
        m_r.drawText(r.x + 18, ty, label, 17, col::text);
        if (desc) {
            float dy = ty + 24;
            for (const auto &line : m_r.wrapText(desc, 12, r.w - 18 - 90)) {
                m_r.drawText(r.x + 18, dy, line, 12, col::muted);
                dy += 16;
            }
        }
        if (toggle) {
            // A switch.
            Rect sw{r.x + r.w - 18 - 44, r.y + r.h / 2 - 12, 44, 24};
            m_r.fillRoundRect(sw, 12, on ? col::online : col::faint);
            m_r.fillCircle(on ? sw.x + sw.w - 12 : sw.x + 12, sw.y + 12, 9, Color::hex(0xffffff));
        } else {
            // < Dark >
            std::string v = Settings::themeName(m_settings.theme);
            float vw = m_r.measureText(v, 16);
            float rx = r.x + r.w - 18;
            Color ac = focus ? col::text : col::faint;
            m_r.fillTriangle(rx - 8, r.y + r.h / 2 - 6, rx, r.y + r.h / 2, rx - 8, r.y + r.h / 2 + 6, ac);
            m_r.drawText(rx - 18 - vw, r.y + r.h / 2 - 10, v, 16, col::bright);
            float lx = rx - 18 - vw - 14;
            m_r.fillTriangle(lx, r.y + r.h / 2 - 6, lx - 8, r.y + r.h / 2, lx, r.y + r.h / 2 + 6, ac);
        }
    }
    // About this console, in plain text under the settings.
    {
        const SystemInfo &si = SystemInfo::get();
        const SettingItem &last = items[3];
        float y = last.rect.y + last.rect.h + 18;
        const std::pair<const char *, const std::string *> rows[] = {
            {"HOS version", &si.hos}, {"Platform", &si.platform}, {"App version", &si.app}, {"Environment", &si.environment}};
        float col_w = (c.w - 56) / 2;
        for (int i = 0; i < 4; i++) {
            float x = c.x + 28 + (i % 2) * col_w, ry = y + (i / 2) * 22;
            float lw = m_r.drawText(x, ry, rows[i].first, 13, col::muted);
            m_r.drawText(x + lw + 10, ry, m_r.ellipsize(*rows[i].second, 13, col_w - lw - 20), 13, col::text);
        }
    }
    const char *hints = m_mouseMode ? "Click a setting to change it"
                                    : "(Up/Down) Choose   (A) Change   (Left/Right) Pick theme   (B or -) Close";
    m_r.drawText(c.x + c.w / 2 - m_r.measureText(hints, 13) / 2, c.y + c.h + 10, hints, 13, Color::hex(0xdcdee3));
}

discord::MentionLookup App::mentionLookup(const Message *m)
{
    discord::MentionLookup lk;
    Channel *ch = m_openChannel ? m_client.findChannel(m_openChannel) : nullptr;
    Guild *g = ch && ch->guild_id ? m_client.findGuild(ch->guild_id) : nullptr;
    lk.user = [this, m, ch](uint64_t id, std::string &name) {
        if (m)
            if (const std::string *n = m->mentionName(id)) {
                name = *n;
                return true;
            }
        if (id == m_client.me().id) {
            name = m_client.me().displayName();
            return true;
        }
        if (ch)
            for (const auto &r : ch->recipients)
                if (r.id == id) {
                    name = r.displayName();
                    return true;
                }
        return false;
    };
    lk.role = [g](uint64_t id, std::string &name, uint32_t &color) {
        if (g)
            for (const auto &r : g->roles)
                if (r.id == id) {
                    name = r.name;
                    color = r.color;
                    return true;
                }
        return false;
    };
    lk.channel = [this](uint64_t id, std::string &name) {
        Channel *c = m_client.findChannel(id);
        if (!c)
            return false;
        name = c->isDm() ? c->label() : c->name;
        return true;
    };
    lk.time = [](int64_t when, char style) {
        LocalTime t{};
        if (!localTime(when, t))
            return std::to_string(when);
        return discord::formatTimestamp(when, unixNow(), style,
                                        {t.year, t.month, t.day, t.hour, t.minute, t.second, t.weekday});
    };
    return lk;
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

    const float text_w = width - kMsgTextX - 24;
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
        if (!system && m.contentIsEmbedLink())
            content.clear();  // the GIF/image below is the message
        discord::MentionText mt = discord::resolveMentions(content, mentionLookup(&m), !system);
        discord::EmoteText et = discord::extractEmotes(mt.text, Renderer::kInlineFirst, Renderer::kInlineLast);
        for (const auto &sp : mt.spans) {
            MessageLayout::SpanStyle st{col::mention_fg, col::mention_bg};
            if (sp.kind == discord::MentionKind::Role && sp.color)
                st = {Color::hex(sp.color), Color::hex(sp.color, 26)};
            else if (sp.kind == discord::MentionKind::Time)
                st = {L.text_color, col::overlay};
            L.span_styles.push_back(st);
        }
        if (!m.pending && !m.failed) {
            Snowflake me = m_client.me().id;
            L.mentions_me = m.mention_everyone ||
                            std::find(m.mention_users.begin(), m.mention_users.end(), me) != m.mention_users.end();
            if (!L.mentions_me && !m.mention_roles.empty() && m.guild_id)
                if (Guild *g = m_client.findGuild(m.guild_id))
                    for (Snowflake r : m.mention_roles)
                        if (std::find(g->my_roles.begin(), g->my_roles.end(), r) != g->my_roles.end())
                            L.mentions_me = true;
        }
        L.text_size = et.jumbo ? kJumboTextSize : kTextSize;
        if (!et.emotes.empty()) {
            // Ask the CDN for a power of two at least the drawn size.
            float drawn = L.text_size * Renderer::kInlineSize * m_r.scale();
            int px = 16;
            while (px < drawn && px < 256)
                px *= 2;
            for (const auto &e : et.emotes)
                L.emotes.push_back(discord::emotePath(e, px));
        }
        if (!et.text.empty())
            L.lines = m_r.wrapText(et.text, L.text_size, text_w);
        L.line_emotes.resize(L.lines.size());
        // Split lines at mention markers into runs.
        L.line_runs.resize(L.lines.size());
        int active = -1;  // mention span carried over from the previous line
        for (size_t li = 0; li < L.lines.size(); li++) {
            const std::string &line = L.lines[li];
            size_t i = 0, seg = 0;
            auto flush = [&](size_t end) {
                if (end > seg) {
                    MessageLayout::Run run;
                    run.x = m_r.measureText(std::string_view(line).substr(0, seg), L.text_size);
                    run.text = line.substr(seg, end - seg);
                    run.w = m_r.measureText(run.text, L.text_size);
                    run.span = active;
                    L.line_runs[li].push_back(std::move(run));
                }
            };
            while (i < line.size()) {
                size_t start = i;
                uint32_t cp = utf8Next(line, i);
                if (cp == discord::kMentionMarkEnd) {
                    flush(start);
                    active = -1;
                    seg = i;
                } else if (cp >= discord::kMentionMarkFirst && cp < discord::kMentionMarkFirst + mt.spans.size()) {
                    flush(start);
                    active = (int)(cp - discord::kMentionMarkFirst);
                    seg = i;
                }
            }
            flush(line.size());
        }
        if (!et.emotes.empty()) {
            for (size_t li = 0; li < L.lines.size(); li++) {
                const std::string &line = L.lines[li];
                size_t i = 0;
                while (i < line.size()) {
                    size_t start = i;
                    uint32_t cp = utf8Next(line, i);
                    if (cp >= Renderer::kInlineFirst && cp < Renderer::kInlineFirst + L.emotes.size())
                        L.line_emotes[li].push_back(
                            {m_r.measureText(std::string_view(line).substr(0, start), L.text_size),
                             cp - Renderer::kInlineFirst});
                }
            }
        }
        if (m.has_reply) {
            std::string r = (m.reply_author.empty() ? "" : "@" + m.reply_author + " ") +
                            discord::emotesAsText(discord::resolveMentions(m.reply_content, mentionLookup(&m), false).text);
            for (auto &c : r)
                if (c == '\n')
                    c = ' ';
            L.reply = m_r.ellipsize(r, 13, text_w);
        }
        const float img_max_w = std::min(400.0f, text_w), img_max_h = 300;
        auto addImage = [&](const std::string &source, const std::string &name, int sw, int sh, bool spoiler,
                            const std::string &animated) {
            ImageBlock b;
            b.source = source;
            b.animated = animated;
            b.name = name;
            b.src_w = sw;
            b.src_h = sh;
            b.spoiler = spoiler;
            fitImage(sw, sh, img_max_w, img_max_h, false, b.w, b.h);
            float s = m_r.scale();
            b.url = discord::cdn::sizedImageUrl(source, (int)std::ceil(b.w * s), (int)std::ceil(b.h * s));
            L.images.push_back(std::move(b));
        };
        for (const auto &a : m.attachments)
            if (a.isImage() && L.images.size() < 10)
                addImage(discord::cdn::attachmentImageUrl(a), a.filename, a.width, a.height, a.spoiler, {});
        for (const auto &e : m.embed_images) {
            addImage(e.url, {}, e.width, e.height, false, e.animated_url);
            // GIF embeds we can't animate: say where they come from, once, so
            // support for the provider can be added.
            if (e.gifv && e.animated_url.empty()) {
                static std::set<std::string> logged;
                if (logged.size() < 50 && logged.insert(e.video_url).second)
                    logf("app: can't animate a %s GIF: video %s, still %s",
                         e.provider.empty() ? "(unknown provider)" : e.provider.c_str(), e.video_url.c_str(),
                         discord::cdn::imageCacheKey(e.url).c_str());
            }
        }

        for (const auto &a : m.attachments) {
            if (a.isImage())
                continue;
            char size_buf[32];
            snprintf(size_buf, sizeof(size_buf), " (%.1f KB)", a.size / 1024.0);
            L.extras.push_back({"[file] " + a.filename + size_buf, col::link});
        }
        if (m.embed_count && !(m.embed_summary.empty() && !m.embed_images.empty())) {
            std::string e = m.embed_summary.empty() ? "[embed]" : "[embed] " + m.embed_summary;
            for (auto &c : e)
                if (c == '\n')
                    c = ' ';
            L.extras.push_back({m_r.ellipsize(e, kSmallSize, text_w), col::muted});
        }
        if (!m.uploading.empty())
            L.extras.push_back({(m.pending ? "Uploading " : "Not sent: ") + m.uploading + (m.pending ? "..." : ""),
                                m.pending ? col::muted : col::danger});
        if (m.failed)
            L.extras.push_back({m.error.empty() ? "Failed to send." : "Failed to send: " + m.error, col::danger});

        float h = 0;
        if (L.show_header)
            h += 14 + lh;  // gap + name row
        if (!L.reply.empty())
            h += 20;
        h += L.lines.size() * m_r.lineHeight(L.text_size);
        for (const auto &img : L.images)
            h += img.h + 6;
        if (!L.images.empty() && !L.lines.empty())
            h += 4;
        h += L.extras.size() * (kSmallSize * 1.5f);
        h += L.show_header ? 4 : 2;
        if (L.show_header)  // room for the avatar
            h = std::max(h, 14 + (L.reply.empty() ? 0.0f : 20.0f) + 19 + kMsgAvatarR + 6);
        L.height = h;
        L.top = m_layoutHeight;
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
        std::string topic = discord::emotesAsText(discord::resolveMentions(ch->topic, mentionLookup(nullptr), false).text);
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
    const int sel = selectedIndex();
    if (m_selActive && sel < 0 && !m_layout.empty())
        m_selActive = false;  // the message went away
    if (sel >= 0 && m_selScrollPending) {
        // Keep the highlighted message in view, with a margin.
        m_selScrollPending = false;
        const MessageLayout &S = m_layout[sel];
        const float margin = 24;
        // m_msgScroll is the distance from the bottom: at least min_scroll
        // shows its top, at most max_sel_scroll shows its bottom.
        float min_scroll = m_layoutHeight - S.top - view.h + margin;
        float max_sel_scroll = m_layoutHeight - (S.top + S.height) - margin;
        if (S.height + 2 * margin > view.h)
            m_msgScroll = min_scroll;  // taller than the view: show its top
        else
            m_msgScroll = std::clamp(m_msgScroll, min_scroll, max_sel_scroll);
        m_msgScroll = std::max(0.0f, m_msgScroll);
    }
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
    const float tx = view.x + kMsgTextX;
    m_imageHits.clear();
    for (size_t li = 0; li < m_layout.size(); li++) {
        const MessageLayout &L = m_layout[li];
        if (y + L.height < view.y || y > view.y + view.h) {
            y += L.height;
            continue;
        }
        float cy = y;
        if ((int)li == sel) {
            m_r.fillRect({view.x, y, view.w, L.height}, col::overlay);
            m_r.fillRect({view.x, y, 3, L.height}, col::accent);
        }
        if (L.mentions_me) {
            m_r.fillRect({view.x, y, view.w, L.height}, Color(240, 177, 50, 20));
            m_r.fillRect({view.x, y, 2, L.height}, col::warn);
        }
        if (L.show_header) {
            cy += 14;
            if (!L.reply.empty()) {
                m_r.fillRect({view.x + 34, cy + 8, 2, 12}, col::faint);
                m_r.fillRect({view.x + 34, cy + 8, 28, 2}, col::faint);
                m_r.drawText(tx, cy + 1, L.reply, 13, col::muted);
                cy += 20;
            }
            drawAvatar(view.x + kMsgAvatarX, cy + 19, kMsgAvatarR, L.author, L.author_id, L.avatar);
            float nw = m_r.drawText(tx, cy, m_r.ellipsize(L.author, kTextSize, view.w / 2), kTextSize, col::bright);
            m_r.drawText(tx + nw + 10, cy + 3, L.time, 12, col::faint);
            cy += lh;
        }
        const float line_h = m_r.lineHeight(L.text_size);
        const float emote = L.text_size * Renderer::kInlineSize;
        const float slot = L.text_size * Renderer::kInlineAdvance;
        for (size_t li = 0; li < L.lines.size(); li++) {
            for (const auto &run : L.line_runs[li]) {
                if (run.span < 0) {
                    m_r.drawText(tx + run.x, cy, run.text, L.text_size, L.text_color);
                    continue;
                }
                const auto &st = L.span_styles[run.span];
                m_r.fillRoundRect({tx + run.x - 2, cy + 1, run.w + 4, line_h - 2}, 4, st.bg);
                m_r.drawText(tx + run.x, cy, run.text, L.text_size, st.fg);
            }
            for (const auto &pe : L.line_emotes[li]) {
                Rect r{tx + pe.first + (slot - emote) / 2, cy + (line_h - emote) / 2, emote, emote};
                if (r.y + r.h < view.y || r.y > view.y + view.h)
                    continue;
                if (GLuint tex = m_images.get(L.emotes[pe.second]))
                    m_r.drawImage(tex, r, Shape::Rect);
                else
                    m_r.fillRoundRect(r, 4, col::bg_input);
            }
            cy += line_h;
        }
        if (!L.images.empty() && !L.lines.empty())
            cy += 4;
        for (size_t ii = 0; ii < L.images.size(); ii++) {
            const ImageBlock &img = L.images[ii];
            Rect r{tx, cy + 2, img.w, img.h};
            cy += img.h + 6;
            if (r.y + r.h < view.y || r.y > view.y + view.h)
                continue;
            bool hidden = img.spoiler && !m_revealed.count(img.source);
            bool failed = false;
            // GIF embeds: the still shows until the animation has loaded.
            bool anim_failed = false;
            GLuint tex = hidden || img.animated.empty() ? 0 : m_images.get(img.animated, &anim_failed);
            if (anim_failed) {
                static std::set<std::string> logged;
                if (logged.size() < 50 && logged.insert(img.animated).second)
                    logf("app: no GIF at %s, showing the still", img.animated.c_str());
            }
            if (!tex && !hidden)
                tex = m_images.get(img.url, &failed);
            if (tex) {
                m_r.drawImage(tex, r, Shape::Rounded);
                if (hovering(r))
                    m_r.fillRoundRect(r, 6, col::overlay);
            } else {
                m_r.fillRoundRect(r, 6, hovering(r) ? col::hover : col::bg_input);
                const char *label = hidden ? "SPOILER" : failed ? "Image unavailable" : "Loading image...";
                float size = hidden ? 15 : 13;
                m_r.drawText(r.x + r.w / 2 - m_r.measureText(label, size) / 2, r.y + r.h / 2 - size * 0.7f, label,
                             size, hidden ? col::bright : col::faint);
            }
            // Only the visible part counts for taps.
            float top = std::max(r.y, view.y), bottom = std::min(r.y + r.h, view.y + view.h);
            m_imageHits.push_back({{r.x, top, r.w, bottom - top}, li, ii});
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
        m_r.drawText(pill.x + pill.w / 2 - m_r.measureText(t, 13) / 2, pill.y + 6, t, 13, col::on_accent);
    }

    // Input bar: the draft (or a placeholder) and a send button.
    Rect input = inputBoxRect();
    bool can_send = m_client.canSend(*ch);
    m_r.fillRoundRect(input, 8, col::bg_input);
    uint64_t cooldown = can_send ? m_client.sendCooldownMs(*ch) : 0;
    Draft *dr = currentDraft();
    std::string draft = dr ? dr->text : std::string();
    for (auto &c : draft)
        if (c == '\n')
            c = ' ';
    bool has_text = draft.find_first_not_of(' ') != std::string::npos;
    bool has_draft = draftHasContent(dr);
    // Left: a "+" for attaching, then the attached screenshot with an x.
    float text_x = input.x + 16;
    if (can_send) {
        Rect a = attachButtonRect();
        float cx = a.x + a.w / 2, cy = a.y + a.h / 2;
        m_r.fillCircle(cx, cy, 11, hovering(a) ? col::bright : col::muted);
        m_r.fillRect({cx - 5.5f, cy - 1, 11, 2}, col::bg_input);
        m_r.fillRect({cx - 1, cy - 5.5f, 2, 11}, col::bg_input);
        text_x = a.x + a.w + 8;
        if (dr && dr->has_image) {
            Rect t{input.x + 42, input.y + 5, 53, 30};
            if (GLuint tex = m_images.get(dr->image.key))
                m_r.drawImage(tex, t, Shape::Rounded);
            else
                m_r.fillRoundRect(t, 4, col::selected);
            Rect x = removeImageRect();
            float xc = x.x + x.w / 2, yc = x.y + x.h / 2;
            m_r.fillCircle(xc, yc, 9, hovering(x) ? col::danger : col::selected);
            const char *times = "\xC3\x97";  // U+00D7
            m_r.drawText(xc - m_r.measureText(times, 15) / 2, yc - 10, times, 15, hovering(x) ? col::on_accent : col::text);
            text_x = x.x + x.w + 10;
        }
    }
    const float text_w = input.x + input.w - 16 - (can_send ? 40 : 0) - text_x;
    if (has_text) {
        // Show the end of a long draft, where the user was typing.
        std::string shown = draft;
        if (m_r.measureText(shown, 16) > text_w) {
            size_t cut = 0;
            while (cut < shown.size() && m_r.measureText(std::string_view(shown).substr(cut), 16) >
                                             text_w - m_r.measureText("...", 16)) {
                cut++;
                while (cut < shown.size() && ((unsigned char)shown[cut] & 0xC0) == 0x80)
                    cut++;
            }
            shown = "..." + shown.substr(cut);
        }
        m_r.drawText(text_x, input.y + 10, shown, 16, can_send ? col::text : col::faint);
    } else {
        std::string placeholder = !can_send          ? "You can't send messages here."
                                  : has_draft      ? (m_mouseMode ? "Add a message" : "(X) Add a message")
                                  : m_mouseMode    ? "Message " + title
                                                   : "(X) Message " + title;
        if (can_send && ch->rate_limit_per_user > 0)
            placeholder += "  (slowmode " + std::to_string(ch->rate_limit_per_user) + "s)";
        m_r.drawText(text_x, input.y + 10, m_r.ellipsize(placeholder, 16, text_w), 16, col::faint);
    }
    if (can_send) {
        Rect b = sendButtonRect();
        bool ready = has_draft && !cooldown;
        Color bg = !ready ? Color(0, 0, 0, 0) : hovering(b) ? col::accent_hover : col::accent;
        if (ready)
            m_r.fillRoundRect(b, 6, bg);
        // An arrow.
        float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        Color fg = ready ? col::on_accent : col::faint;
        m_r.fillTriangle(cx - 7, cy - 8, cx + 9, cy, cx - 7, cy + 8, fg);
    }

    // Under the box: who's typing, or slowmode.
    float uy = input.y + input.h + 3;
    std::string typing = m_client.typingText(m_openChannel);
    if (!typing.empty()) {
        // Three bouncing dots, then the names.
        for (int d = 0; d < 3; d++) {
            float phase = (float)((m_frameTime + d * 150) % 900) / 900.0f;
            float a = 0.4f + 0.6f * std::max(0.0f, std::sin(phase * 6.2832f));
            m_r.fillCircle(input.x + 10 + d * 8, uy + 9, 2.6f, Color(col::text.r, col::text.g, col::text.b, (uint8_t)(a * 255)));
        }
        typing = m_r.ellipsize(typing, 13, input.w - 120);
        m_r.drawText(input.x + 36, uy, typing, 13, col::muted);
    }
    if (!m_composeError.empty() && m_frameTime < m_composeErrorUntil) {
        std::string e = m_r.ellipsize(m_composeError, 13, input.w / 2);
        m_r.drawText(input.x + input.w / 2 - m_r.measureText(e, 13) / 2, uy, e, 13, col::danger);
    }
    if (cooldown) {
        std::string slow = "Slowmode: " + std::to_string(cooldown / 1000 + 1) + "s";
        m_r.drawText(input.x + input.w - m_r.measureText(slow, 13), uy, slow, 13, col::warn);
    }
}

// --- Image viewer ------------------------------------------------------------

void App::fitImage(int src_w, int src_h, float max_w, float max_h, bool allow_up, float &w, float &h)
{
    if (src_w <= 0 || src_h <= 0) {
        // Unknown size: a 16:9 box.
        w = std::min(max_w, max_h * 16 / 9);
        h = w * 9 / 16;
        return;
    }
    float k = std::min(max_w / src_w, max_h / src_h);
    if (!allow_up)
        k = std::min(k, 1.0f);
    w = std::max(16.0f, std::floor(src_w * k));
    h = std::max(16.0f, std::floor(src_h * k));
}

void App::openViewer(size_t layout, size_t image)
{
    m_viewerImages.clear();
    m_viewerIndex = 0;
    for (size_t li = 0; li < m_layout.size(); li++) {
        for (size_t ii = 0; ii < m_layout[li].images.size(); ii++) {
            if (li == layout && ii == image)
                m_viewerIndex = m_viewerImages.size();
            m_viewerImages.push_back(m_layout[li].images[ii]);
        }
    }
    if (m_viewerImages.empty())
        return;
    m_revealed.insert(m_viewerImages[m_viewerIndex].source);
    m_viewerOpen = true;
}

Rect App::viewerSaveRect() const
{
    return {Renderer::kWidth - 24 - 120, 20, 120, 36};
}

void App::saveViewerImage()
{
    if (m_viewerIndex >= m_viewerImages.size())
        return;
    const ImageBlock &img = m_viewerImages[m_viewerIndex];
    if (img.spoiler && !m_revealed.count(img.source))
        return;
    // The GIF itself for Tenor/Giphy; otherwise the full-size original.
    std::string url = !img.animated.empty() ? img.animated : img.source;
    std::string name = img.name;
    if (name.empty()) {
        // The last part of the URL's path.
        std::string p = url.substr(0, url.find('?'));
        size_t slash = p.rfind('/');
        name = slash == std::string::npos ? p : p.substr(slash + 1);
    }
    m_saver.save(url, name);
    toast("Saving...");
}

void App::toast(const std::string &msg, bool ok)
{
    m_toast = msg;
    m_toastOk = ok;
    m_toastUntil = m_frameTime + (ok ? 2500 : 4000);
}

void App::drawToast()
{
    if (m_toast.empty() || m_frameTime >= m_toastUntil)
        return;
    float tw = m_r.measureText(m_toast, 15);
    Rect r{Renderer::kWidth / 2 - tw / 2 - 18, 18, tw + 36, 34};
    m_r.fillRoundRect(r, 17, m_toastOk ? Color::hex(0x248046) : col::danger);
    m_r.drawText(r.x + 18, r.y + 7, m_toast, 15, col::on_accent);
}

void App::handleViewerInput(u64 down)
{
    if (down & HidNpadButton_Y) {
        saveViewerImage();
        return;
    }
    if (down & (HidNpadButton_B | HidNpadButton_A)) {
        m_viewerOpen = false;
        return;
    }
    int step = 0;
    if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_L | HidNpadButton_ZL))
        step = -1;
    if (down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_R | HidNpadButton_ZR))
        step = 1;
    if (step) {
        long next = (long)m_viewerIndex + step;
        if (next >= 0 && next < (long)m_viewerImages.size()) {
            m_viewerIndex = (size_t)next;
            m_revealed.insert(m_viewerImages[m_viewerIndex].source);
        }
    }
}

void App::drawViewer()
{
    const float W = Renderer::kWidth, H = Renderer::kHeight;
    m_r.fillRect({0, 0, W, H}, Color(0, 0, 0, 225));
    if (m_viewerIndex >= m_viewerImages.size())
        return;
    const ImageBlock &img = m_viewerImages[m_viewerIndex];

    float w, h;
    fitImage(img.src_w, img.src_h, W - 100, H - 120, true, w, h);
    // Don't blow tiny images up past twice their size.
    if (img.src_w > 0 && w > img.src_w * 2) {
        h *= img.src_w * 2 / w;
        w = img.src_w * 2.0f;
    }
    Rect r{std::floor(W / 2 - w / 2), std::floor((H - 50) / 2 - h / 2), w, h};

    float s = m_r.scale();
    int rw = std::min((int)std::ceil(w * s), ImageCache::kMaxImageSide);
    int rh = std::min((int)std::ceil(h * s), ImageCache::kMaxImageSide);
    if (img.src_w > 0) {
        rw = std::min(rw, img.src_w);
        rh = std::min(rh, img.src_h);
    }
    bool failed = false;
    GLuint full = !img.animated.empty() ? m_images.get(img.animated, &failed)
                                        : m_images.get(discord::cdn::sizedImageUrl(img.source, rw, rh), &failed);
    // While the big version loads, stretch the list-sized one.
    GLuint tex = full ? full : m_images.get(img.url);
    if (tex)
        m_r.drawImage(tex, r, Shape::Rounded);
    else
        m_r.fillRoundRect(r, 6, col::bg_channels);
    if (!full) {
        const char *label = failed ? "Couldn't load the full image" : "Loading...";
        float lw = m_r.measureText(label, 13);
        Rect pill{W / 2 - lw / 2 - 12, r.y + r.h - 34, lw + 24, 24};
        m_r.fillRoundRect(pill, 12, Color(0, 0, 0, 170));
        m_r.drawText(pill.x + 12, pill.y + 4, label, 13, Color::hex(0xdcdee3));
    }

    std::string caption = img.name.empty() ? "Image" : img.name;
    if (m_viewerImages.size() > 1)
        caption += "   " + std::to_string(m_viewerIndex + 1) + " / " + std::to_string(m_viewerImages.size());
    caption = m_r.ellipsize(caption, kSmallSize, W / 2);
    // The viewer is always dark, whatever the theme.
    m_r.drawText(24, H - 34, caption, kSmallSize, Color::hex(0xdcdee3));
    std::string hint = m_mouseMode ? "(Click) Close"
                       : m_viewerImages.size() > 1 ? "(Left/Right) Previous / next   (Y) Save   (B) Close"
                                                   : "(Y) Save   (B) Close";
    if (!(img.spoiler && !m_revealed.count(img.source))) {
        Rect sb = viewerSaveRect();
        m_r.fillRoundRect(sb, 8, hovering(sb) ? col::accent_hover : col::accent);
        const char *st = m_saver.busy() ? "Saving..." : "Save";
        m_r.drawText(sb.x + sb.w / 2 - m_r.measureText(st, 15) / 2, sb.y + 8, st, 15, col::on_accent);
    }
    m_r.drawText(W - 24 - m_r.measureText(hint, kSmallSize), H - 34, hint, kSmallSize, Color::hex(0x959ba5));
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
    case Pane::Guilds: hints = "(A) Open   (L/R) Server   (-) Settings   (+) Exit"; break;
    case Pane::Channels: hints = "(A) Open   (B) Back   (L/R) Server   (-) Settings   (+) Exit"; break;
    case Pane::Messages:
    {
        int sel = selectedIndex();
        bool image = sel >= 0 ? !m_layout[sel].images.empty() : !m_imageHits.empty();
        hints = std::string(image ? "(A) View image   " : "") + "(X) Write/attach   (Y) Send   (Up/Down/RS) Select   (ZL/ZR) Page   " +
                (sel >= 0 ? "(RS click) Latest   " : "") + "(B) Back";
    }
        break;
    }
    m_r.drawText(area.x + area.w - 18 - m_r.measureText(hints, 13), area.y + 7, hints, 13, col::faint);
}
