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
#include "media/fetch.h"
#include "media/player.h"
#include "settings.h"
#include "ui/album.h"
#include "ui/images.h"
#include "ui/saver.h"
#include "ui/render.h"
#include "util/qr.h"
#include "util/scroller.h"

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
        const char *kind = nullptr;  // built-in icon before the name ("hash", "voice"...)
        int indent = 0;              // threads sit under their channel
        bool member = false;  // someone in the voice channel above (id: the user), not selectable
        bool selectable() const { return !header && !member; }
    };

    // A picture in a message (attachment or embed image).
    struct ImageBlock {
        std::string source;  // proxy URL, unsized
        std::string url;     // sized for the message list
        std::string name;    // file name, if any
        std::string animated;  // Tenor/Giphy GIF to play instead of the still
        std::string video;     // a video attachment's URL: the picture is its poster
        uint64_t video_size = 0;
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
        std::string username;  // account name, for the profile card
        bool bot = false, webhook = false;
        bool show_header = false;
        float height = 0;
        float top = 0;  // from the top of the whole layout
        std::string author, time;
        Color author_color;  // highest coloured role, else the normal name colour
        std::vector<std::pair<std::string, Color>> extras;  // reply/attachments/embeds
        std::string reply;
        float reply_w = 0;   // width the reply line is cut to
        Color text_color;
        std::vector<ImageBlock> images;
        // The text (markdown laid out): styled runs at positions relative to
        // the text's top left, plus code block boxes and quote bars.
        float text_size = 17;  // larger for emote-only ("jumbo") messages
        std::vector<std::string> emotes;  // CDN paths
        struct Run {
            float x = 0, y = 0, w = 0, h = 0;  // h: the line's height
            float size = 17;
            std::string text;
            uint16_t style = 0;  // discord::md::Style, plus kCodeBlockRun
            int span = -1;       // mention: index into span_styles
            int spoiler = -1;
            bool bold = false;   // headings
            Color fg;
        };
        struct EmoteAt {
            float x = 0, y = 0, size = 0;
            uint32_t index = 0;
            int spoiler = -1;
        };
        struct SpanStyle {
            Color fg, bg;
        };
        std::vector<Run> runs;
        std::vector<EmoteAt> emote_at;
        std::vector<Rect> code_boxes, quote_bars;
        std::vector<SpanStyle> span_styles;
        float text_h = 0;
        int spoilers = 0;
        bool mentions_me = false;  // drawn with a highlighted background
        bool can_reply = false;    // a sent, ordinary message
        // Reactions, as chips under the message (rows wrap at the text width).
        struct Chip {
            Reaction reaction;
            std::string image;  // CDN path or Twemoji URL, empty if none
            std::string count;
            float x = 0, w = 0;
            int row = 0;
        };
        std::vector<Chip> chips;
        // A thread started from this message, shown as a box under it.
        Snowflake thread_id = 0;
        std::string thread_name, thread_info;
    };
    struct ReactionHit {
        Rect rect;
        size_t layout = 0, chip = 0;
    };
    struct SpoilerHit {
        Rect rect;
        size_t layout = 0;
        int spoiler = 0;
    };
    // Lays out a message's text (markdown) into L.runs etc.
    void layoutText(MessageLayout &L, const Message &m, const std::string &content, bool system, float width);
    std::string spoilerKey(const MessageLayout &L, int spoiler) const;
    bool revealSpoilers(size_t layout);  // false if none were hidden
    // A focusable part of the highlighted message, picked with left/right.
    enum class PartKind { None, Avatar, Reply, Image, Thread, Reaction };
    struct Part {
        PartKind kind = PartKind::None;
        size_t index = 0;  // image or chip
        bool operator==(const Part &o) const { return kind == o.kind && index == o.index; }
    };
    // Focusable things in the bar under the messages, picked with left/right.
    enum class ComposeItem { Attach, RemoveImage, Text, Emoji, Send, CancelReply };
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
    float m_guildScroll = 0;
    int m_channelIndex = 0;
    float m_channelScroll = 0;
    Snowflake m_openChannel = 0;
    // A forum/media channel shows its posts while no post is open; a post
    // (a thread) opens as a chat, and B goes back to the posts.
    Snowflake m_openForum = 0;
    // A thread opened from a message: B goes back to that channel.
    Snowflake m_threadReturn = 0;
    // Where the user last was: saved so the next launch reopens it, and used
    // to put a thread back after a reconnect drops it from the lists.
    struct LastPlace {
        Snowflake user = 0, guild = 0, channel = 0;
        Snowflake parent = 0;  // a thread's channel or forum
        Snowflake ret = 0;     // the channel a thread was opened from
        std::string name;      // a thread's name
    };
    LastPlace m_place;
    bool m_restorePending = false;  // reopen m_place once the client is ready
    uint64_t m_restoreSince = 0;
    uint32_t m_seenConnections = 0;
    bool m_woke = false;  // set by the applet hook on resume from sleep
    AppletHookCookie m_appletHook{};
    static void onAppletHook(AppletHookType type, void *param);
    void trackPlace();
    void restorePlace();
    void refreshAfterReconnect();
    int m_forumIndex = 0;
    float m_forumScroll = 0, m_forumContentH = 0;
    bool m_forumScrollPending = false;
    Scroller m_forumScroller;
    std::vector<Snowflake> m_forumPosts;                 // in display order
    std::vector<std::pair<Rect, size_t>> m_forumHits;   // cards drawn this frame
    std::vector<std::pair<Rect, size_t>> m_threadHits;  // thread boxes drawn this frame (layout index)
    bool inForum() const { return m_openForum && !m_openChannel; }
    void openForum(Snowflake id);
    void openThread(Snowflake thread_id, Snowflake parent_id, const std::string &name);
    void buildForumPosts();
    void drawForum(Rect area);
    std::vector<ChannelRow> m_rows;
    uint32_t m_rowsVersion = ~0u;
    uint32_t m_rowsVoiceVersion = ~0u;
    int m_rowsGuild = -1;

    // Messages pane.
    float m_msgScroll = 0;  // distance from the bottom, in logical px
    std::vector<MessageLayout> m_layout;
    float m_layoutHeight = 0;
    uint32_t m_layoutVersion = ~0u;
    Snowflake m_layoutChannel = 0;
    std::vector<ImageHit> m_imageHits;    // images drawn this frame
    std::set<std::string> m_revealed;     // spoilers the user opened
    std::vector<ReactionHit> m_reactionHits;              // chips drawn this frame
    std::vector<SpoilerHit> m_spoilerHits;                // hidden spoilers drawn this frame
    std::vector<std::pair<Rect, size_t>> m_messageHits;  // visible messages, for taps
    std::vector<std::pair<Rect, size_t>> m_avatarHits;   // avatars drawn this frame
    Rect m_replyButton{};                                 // on the highlighted/hovered message
    int m_replyButtonLayout = -1;                         // -1: not shown
    Part m_selPart;                       // part of the highlighted message with focus
    bool m_composeFocus = false;          // focus is in the text box row
    ComposeItem m_composeSel = ComposeItem::Text;
    // Buttons below the channel list: Disconnect on the voice panel (while
    // in a call), then Mute, Deafen and Settings on the user panel.
    enum class PanelFocus { None, Disconnect, Status, Mute, Deafen, Gear };
    PanelFocus m_panelFocus = PanelFocus::None;
    // The channel list is one block for the controller until (A) goes into
    // it, so Down reaches the panel buttons below it at once.
    bool m_channelsEntered = false;
    bool panelFocused() const { return m_panelFocus != PanelFocus::None; }
    void activatePanel(PanelFocus f);
    void drawVoicePanel(Rect area);
    void drawPanelButton(PanelFocus f, const char *icon, bool off);
    void drawVoiceMember(const Guild &g, Snowflake user_id, Rect r, const std::set<Snowflake> &speaking);
    float bottomPanelH() const;  // user panel, plus the voice panel in a call
    Rect panelButtonRect(PanelFocus f) const;
    std::vector<Part> messageParts(size_t layout) const;
    bool partFocused(size_t layout, PartKind kind, size_t index = 0) const;
    bool stepPart(int dir);     // along the focused row; false at either end
    // Controller focus moves like on a web page: up/down go to the row
    // above or below (within a message, then the next message, then the
    // text box row), landing on what is nearest to the column we came from.
    struct NavSpot {
        Part part;            // in a message
        ComposeItem item{};   // in the text box rows
        float x = 0;          // centre, on screen
        int row = 0;          // top to bottom within the message (or text box rows)
    };
    static int stepInRow(const std::vector<NavSpot> &spots, size_t at, int dir);
    static int nearestInRow(const std::vector<NavSpot> &spots, int row, float x);
    std::vector<NavSpot> messageSpots(size_t layout) const;
    std::vector<NavSpot> composeSpots();
    struct NavKey {
        bool compose = false;
        ComposeItem item{};
        Snowflake message = 0;
        Part part;
        bool operator==(const NavKey &o) const
        {
            return compose == o.compose && (compose ? item == o.item : message == o.message && part == o.part);
        }
    };
    NavKey navKey() const;
    float focusX();
    float m_navX = 0;     // the column up/down keeps to
    NavKey m_navAt;       // where m_navX was last set (recomputed if focus moved some other way)
    bool m_navValid = false;
    void moveSpatial(int dir);
    void selectMessageAt(int layout, Part part);
    int newestVisibleMessage() const;
    void activatePart();
    std::vector<ComposeItem> composeItems();
    bool composeFocused(ComposeItem item) const;
    bool stepCompose(int dir);  // along the focused row; false at either end
    void composeActivate(ComposeItem item);
    // The controller's focus: an animated gradient outline just outside
    // what has the focus, following its shape (round, squircle or rounded box).
    // r is the focused thing's own rectangle and radius its own corner
    // radius, so the outline keeps the same distance from it all round.
    void drawFocusRing(Rect r, float radius);
    void drawFocusCircle(float cx, float cy, float radius);
    void drawFocusShape(Rect r, Shape shape, float radius = 0);
    // Profile card for a message's author.
    struct Profile {
        Snowflake id = 0, guild = 0;
        std::string name, username, avatar;
        Color color;
        bool bot = false, webhook = false;
    };
    bool m_profileOpen = false;
    Profile m_profile;
    float m_profileScroll = 0, m_profileContentH = 0;
    void openProfile(size_t layout);
    Rect profileCardRect() const;
    void drawProfile();
    // Unsent text (and screenshot) per channel: the keyboard and the picker
    // fill it, (Y) or the send button sends it.
    struct Draft {
        std::string text;
        bool has_image = false;
        Album::Item image;
        Snowflake reply_to = 0;  // message being replied to
        std::string reply_name;
    };
    std::map<Snowflake, Draft> m_drafts;
    std::string m_composeError;  // shown under the text box for a while
    uint64_t m_composeErrorUntil = 0;

    // The (X) menu: write a message, attach or remove a screenshot.
    enum class ComposeAction { Write, Emoji, Attach, Remove, CancelReply };
    bool m_composeMenuOpen = false;
    int m_composeMenuIndex = 0;

    // Emote picker: the emotes we may send here, by server and category.
    struct EmojiItem {
        std::string insert;  // added to the draft: ":name:" for custom emotes, the emoji itself
        std::string image;   // CDN path or Twemoji URL
        std::string name;    // shown under the grid
    };
    struct EmojiSection {
        std::string title;
        std::vector<EmojiItem> items;
    };
    struct EmojiCell {
        Rect rect;  // in content space (before scrolling)
        size_t section = 0, item = 0;
    };
    bool m_emojiOpen = false;
    std::vector<EmojiSection> m_emojiSections;
    std::vector<EmojiCell> m_emojiCells;  // flat, in order
    std::vector<std::pair<float, std::string>> m_emojiHeaders;  // y, title
    float m_emojiContentH = 0;
    int m_emojiIndex = 0;  // into m_emojiCells
    float m_emojiScroll = 0;
    bool m_emojiScrollPending = false;
    Scroller m_emojiScroller;
    std::string m_emojiQuery;
    void openEmojiPicker();
    void buildEmojiSections();  // for the open channel and m_emojiQuery
    void layoutEmojiGrid();
    Rect emojiCardRect() const;
    Rect emojiGridRect() const;
    void emojiInsert(int index);
    void emojiMove(int dx, int dy);
    void handleEmojiInput(u64 down);
    void drawEmojiPicker();
    Rect emojiButtonRect() const;  // in the message bar
    // Turns :shortcodes: into emotes and emoji for sending in channel ch.
    std::string convertShortcodes(const std::string &text, const Channel &ch);

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

    // Full-screen video player: downloads the video, then plays it.
    bool m_videoOpen = false;
    ImageBlock m_videoBlock;
    VideoFetch m_videoFetch;
    VideoPlayer m_player;
    std::string m_videoError;
    void openVideo(const ImageBlock &block);
    void closeVideo();
    void updateVideo();  // per frame: takes the download, advances the player
    void handleVideoInput(u64 down);
    void videoTap(float x, float y);
    Rect videoBarRect() const;   // the seek bar
    Rect videoCloseRect() const;
    void drawVideo();

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
    // Scrolling physics (momentum and rubber banding) for touch and mouse drags.
    Scroller m_msgScroller, m_channelScroller, m_guildScroller, m_pickerScroller, m_profileScroller;
    struct ScrollTarget {
        Scroller *scroller;
        float *pos;
        float sign;  // offset change per pixel the pointer moves down
    };
    ScrollTarget scrollTarget(float start_x);
    VelocityTracker m_dragVelocity;
    ScrollTarget m_dragTarget{nullptr, nullptr, 1};  // grabbed on press, released on lift
    bool m_dragStoppedGlide = false;  // the press stopped a glide: not a tap
    float m_frameDt = 1 / 60.0f;      // seconds since the last frame
    int m_guildShown = -1, m_channelShown = -1;  // selection last scrolled into view
    void pointerDown(float x, float y);
    void pointerUp(float y, bool dragged);

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
    uint64_t m_logoutArmedUntil = 0;  // "press again to log out", in settings

    // Settings pane, opened with (-) or the gear by the user panel.
    Settings m_settings;
    bool m_settingsOpen = false;
    int m_settingsIndex = 0;
    // The main page lists the sections (each its own page) and Log out.
    enum class SettingPage { Main, Appearance, Time, Input, Experimental, About };
    enum class SettingRow {
        PageAppearance, PageTime, PageInput, PageExperimental, PageAbout, LogOut,
        Theme, Clock, Date, Mouse, Touch, QrLogin, Videos,
        Info,  // a line of the About page
    };
    struct SettingItem {
        SettingRow row;
        Rect rect;
        int info = 0;  // Info: which line
    };
    SettingPage m_settingsPage = SettingPage::Main;
    void openSettingsPage(SettingPage page);
    void settingsBack();  // to the main page, or closes
    Rect settingsBackRect() const;
    // The back arrow in the chat header, in a forum post or a thread opened
    // from a channel (to return to the forum or the channel).
    bool hasMessagesBack() const;
    Rect messagesBackRect() const;
    std::vector<SettingItem> settingItems() const;  // in screen space (scrolled)
    Rect settingsListRect() const;  // the scrolling part, under the title
    float m_settingsScroll = 0, m_settingsContentH = 0;
    bool m_settingsScrollPending = false;
    Scroller m_settingsScroller;
    Rect settingsGearRect() const;
    Rect settingsCardRect() const;
    void openSettings();
    void changeSetting(SettingRow row, int dir);  // dir: -1/+1 (left/right, pickers only), 0 = A/tap
    void handleSettingsInput(u64 down);
    void drawSettings();
    void applySettings();  // theme etc. after a change

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
    // The status menu, opened from the avatar and name in the user panel.
    bool m_statusMenuOpen = false;
    int m_statusMenuIndex = 0;
    Rect statusMenuRect() const;
    Rect statusMenuItemRect(int i) const;
    void handleStatusMenuInput(u64 down);
    void drawStatusMenu();
    void chooseStatus(int i);
    void saveVoiceButtons();
    // Discord's status marks: a dot, a moon, a bar, a ring.
    void drawStatusDot(float cx, float cy, float r, const std::string &status, Color bg);
    void drawComposeMenu();
    void drawChip(Rect r, const MessageLayout::Chip &c, bool focus);
    void startReply(size_t layout);  // shows the "Replying to" bar
    void toggleReaction(size_t layout, size_t chip);
    bool replying() const;  // the open channel's draft is a reply
    Rect replyBarRect() const;
    Rect cancelReplyRect() const;
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
    void dragScroll(float start_x, float dy, bool rubber = true);
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
    // Draws a built-in icon (tools/pack_assets.py) into r, tinted (masks are
    // white, so the tint is their colour). Nothing while it loads.
    void drawIcon(const char *name, Rect r, Color tint = Color(255, 255, 255));
    // Text with its emoji drawn as Twemoji pictures (fonts have none), cut
    // with "..." to max_w when that's above 0. Returns the width drawn.
    float drawLabel(float x, float y, std::string_view text, float size, Color c, float max_w = 0);
    void drawAvatar(float cx, float cy, float radius, const std::string &name, Snowflake id,
                    const std::string &icon = {});
};
