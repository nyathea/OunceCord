// SPDX-License-Identifier: MIT
//
// The OunceCord UI: login screen, then a three-pane layout (servers,
// channels, messages) driven by the controller or the touch screen.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <switch.h>

#include "discord/auth.h"
#include "discord/client.h"
#include "discord/mentions.h"
#include "discord/remoteauth.h"
#include "ui/album.h"
#include "ui/images.h"
#include "ui/saver.h"
#include "ui/render.h"
#include "util/qr.h"

class App {
public:
    bool init();
    void exit();
    // Runs one frame. Returns false to quit.
    bool frame();

private:
    enum class Screen { Login, Main };
    enum class Pane { Guilds, Channels, Messages };
    enum class LoginStep { Credentials, Mfa };
    enum class LoginAction { EditLogin, EditPassword, Submit, Totp, Sms, Backup, Back };
    enum class Kbd { Message, Line, Password, Digits };

    struct LoginItem {
        LoginAction action;
        Rect rect;
        std::string label;  // field caption, or button text
        std::string value;  // field contents (masked for the password)
        bool field = false;
        bool primary = false;
        bool enabled = true;
    };

    struct ChannelRow {
        Snowflake id = 0;
        std::string label;
        bool header = false;  // category title, not selectable
        bool voice = false;   // shown, but cannot be opened
        std::string icon;     // CDN path for DMs
    };

    // A picture in a message (attachment or embed image).
    struct ImageBlock {
        std::string source;  // proxy URL, unsized
        std::string url;     // sized for the message list
        std::string name;    // file name, if any
        std::string animated;  // Tenor/Giphy GIF to play instead of the still
        float w = 0, h = 0;  // display size in the list
        int src_w = 0, src_h = 0;  // original size (0 if unknown)
        bool spoiler = false;
    };

    struct ImageHit {
        Rect rect;
        size_t layout = 0, image = 0;
    };

    struct MessageLayout {
        Snowflake id = 0;
        Snowflake author_id = 0;
        std::string avatar;  // CDN path
        bool show_header = false;
        float height = 0;
        float top = 0;  // from the top of the whole layout
        std::string author, time;
        std::vector<std::string> lines;
        std::vector<std::pair<std::string, Color>> extras;  // reply/attachments/embeds
        std::string reply;
        Color text_color;
        std::vector<ImageBlock> images;
        // Custom emotes: the text has a placeholder for each (see
        // discord/emotes.h); line_emotes gives where they sit on each line.
        float text_size = 17;  // larger for emote-only ("jumbo") messages
        std::vector<std::string> emotes;  // CDN paths
        std::vector<std::vector<std::pair<float, uint32_t>>> line_emotes;  // x, emote index
        // Each line split into runs of plain text and mentions (span >= 0
        // indexes span_styles); a mention can continue onto the next line.
        struct Run {
            float x = 0, w = 0;
            std::string text;
            int span = -1;
        };
        struct SpanStyle {
            Color fg, bg;
        };
        std::vector<std::vector<Run>> line_runs;
        std::vector<SpanStyle> span_styles;
        bool mentions_me = false;  // drawn with a highlighted background
    };
    // Resolves names for mentions in a message (null: no message context).
    discord::MentionLookup mentionLookup(const Message *m);

    Renderer m_r;
    ImageCache m_images;
    discord::Client m_client;
    discord::Auth m_auth;
    discord::RemoteAuth m_remote;
    bool m_remoteStarted = false;
    qr::Code m_qr;
    uint32_t m_qrVersion = 0;
    PadState m_pad{};
    Screen m_screen = Screen::Login;
    Pane m_pane = Pane::Guilds;
    std::string m_token;

    // Login screen.
    LoginStep m_loginStep = LoginStep::Credentials;
    std::string m_loginName, m_loginPassword;
    int m_loginFocus = 0;
    std::string m_loginError, m_loginInfo;
    bool m_mfaTotp = false, m_mfaSms = false, m_mfaBackup = false, m_mfaWebauthn = false;
    bool m_smsSent = false;
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
    std::vector<ImageHit> m_imageHits;    // images drawn this frame
    std::set<std::string> m_revealed;     // spoilers the user opened
    // Unsent text (and screenshot) per channel: the keyboard and the picker
    // fill it, (Y) or the send button sends it.
    struct Draft {
        std::string text;
        bool has_image = false;
        Album::Item image;
    };
    std::map<Snowflake, Draft> m_drafts;
    std::string m_composeError;  // shown under the text box for a while
    uint64_t m_composeErrorUntil = 0;

    // The (X) menu: write a message, attach or remove a screenshot.
    enum class ComposeAction { Write, Attach, Remove };
    bool m_composeMenuOpen = false;
    int m_composeMenuIndex = 0;

    // Screenshot picker.
    Album m_album;
    bool m_pickerOpen = false;
    int m_pickerTab = 0;  // 0 album, 1 saved
    const std::vector<Album::Item> &pickerItems() const;
    Rect pickerTabRect(int tab) const;
    void setPickerTab(int tab);
    // Saving pictures from the viewer.
    Saver m_saver;
    Rect viewerSaveRect() const;
    void saveViewerImage();
    // A short message at the top of the screen.
    std::string m_toast;
    bool m_toastOk = true;
    uint64_t m_toastUntil = 0;
    void toast(const std::string &msg, bool ok = true);
    void drawToast();
    int m_pickerIndex = 0;
    float m_pickerScroll = 0;
    bool m_pickerScrollPending = false;
    u64 m_pickerRepeatKey = 0;
    uint64_t m_pickerRepeatAt = 0;
    // The message highlighted by the stick/d-pad; none means following the
    // newest messages.
    bool m_selActive = false;
    Snowflake m_selId = 0;
    size_t m_selHint = 0;           // its last known index, for pending messages without ids
    bool m_selScrollPending = false;  // scroll it into view on the next draw
    uint64_t m_stickRepeatAt = 0;

    // Full-screen image viewer.
    bool m_viewerOpen = false;
    std::vector<ImageBlock> m_viewerImages;  // the channel's images, oldest first
    size_t m_viewerIndex = 0;

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
    bool promptText(const char *header, const char *guide, std::string &out, Kbd kind,
                    int max_len = 0);
    std::vector<LoginItem> loginItems() const;
    void handleLoginInput(u64 down);
    void loginActivate(LoginAction action);
    void submitLogin();
    void onAuthResult(const discord::AuthResult &r);
    void finishLogin(const std::string &token);
    void logOut(const std::string &message);
    // Opens the keyboard on the channel's draft; it doesn't send.
    void composeMessage();
    void sendDraft();
    Draft *currentDraft();  // null when there is none
    bool draftHasContent(const Draft *d) const;
    Rect attachButtonRect() const;
    Rect removeImageRect() const;  // the x on the attached screenshot
    std::vector<ComposeAction> composeActions();
    Rect composeMenuRect(size_t count) const;
    void openComposeMenu();
    void composeMenuChoose(ComposeAction a);
    void handleComposeMenuInput(u64 down);
    void drawComposeMenu();
    void openPicker();
    void pickerPick(int index);
    void handlePickerInput(u64 down, u64 held);
    void drawPicker();
    // Grid geometry.
    static constexpr int kPickerCols = 5;
    Rect pickerGridArea() const;
    Rect pickerCellRect(int index) const;  // in screen space, with scroll
    int hitPicker(float x, float y) const;
    bool canSendNow();
    Rect inputBoxRect() const;
    Rect sendButtonRect() const;
    int selectedIndex() const;  // into m_layout, or -1
    void moveMessageSelection(int delta);
    void clearMessageSelection();

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
    void drawQrPanel(Rect area);
    Rect qrPanelRect() const;
    void drawMain();
    void drawGuildBar(Rect area);
    void drawChannelList(Rect area);
    void drawUserPanel(Rect area);
    void drawMessages(Rect area);
    void drawHints(Rect area);
    void openViewer(size_t layout, size_t image);
    void handleViewerInput(u64 down);
    void drawViewer();
    // Fits a src_w x src_h image inside max_w x max_h (no upscaling unless
    // allow_up); unknown sizes get a 16:9 box.
    static void fitImage(int src_w, int src_h, float max_w, float max_h, bool allow_up, float &w, float &h);
    // Draws the image at icon (a CDN path) as a circle, or a coloured circle
    // with the name's first letter while it loads or when there is none.
    void drawAvatar(float cx, float cy, float radius, const std::string &name, Snowflake id,
                    const std::string &icon = {});
};
