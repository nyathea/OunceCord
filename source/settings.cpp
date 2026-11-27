// SPDX-License-Identifier: MIT
#include "settings.h"

#include "config.h"
#include "util/json.h"

const char *Settings::themeName(Theme t)
{
    switch (t) {
    case Theme::Light: return "Light";
    case Theme::Amoled: return "AMOLED";
    default: return "Dark";
    }
}

const char *Settings::dateFormatName(DateFormat f)
{
    switch (f) {
    case DateFormat::MDY: return "MM-DD-YYYY";
    case DateFormat::DMY: return "DD-MM-YYYY";
    default: return "YYYY-MM-DD";
    }
}

std::string Settings::toJson() const
{
    json::Value v = json::Value::object();
    v.set("theme", json::Value::string(theme == Theme::Light ? "light" : theme == Theme::Amoled ? "amoled" : "dark"));
    v.set("mouse", json::Value::boolean(mouse));
    v.set("touch", json::Value::boolean(touch));
    v.set("qr_login", json::Value::boolean(qr_login));
    v.set("videos", json::Value::boolean(videos));
    v.set("clock24", json::Value::boolean(clock24));
    v.set("date_format", json::Value::string(date_format == DateFormat::MDY   ? "mdy"
                                             : date_format == DateFormat::DMY ? "dmy"
                                                                              : "ymd"));
    v.set("status", json::Value::string(status));
    v.set("self_mute", json::Value::boolean(self_mute));
    v.set("self_deaf", json::Value::boolean(self_deaf));
    return v.dump();
}

Settings Settings::fromJson(const std::string &text)
{
    Settings s;
    json::Value v;
    if (!json::parse(text, v) || !v.isObject())
        return s;
    std::string theme = v["theme"].str();
    if (theme == "light")
        s.theme = Theme::Light;
    else if (theme == "amoled")
        s.theme = Theme::Amoled;
    if (v["mouse"].isBool())
        s.mouse = v["mouse"].asBool();
    if (v["touch"].isBool())
        s.touch = v["touch"].asBool();
    if (v["qr_login"].isBool())
        s.qr_login = v["qr_login"].asBool();
    if (v["videos"].isBool())
        s.videos = v["videos"].asBool();
    if (v["clock24"].isBool())
        s.clock24 = v["clock24"].asBool();
    std::string df = v["date_format"].str();
    if (df == "mdy")
        s.date_format = DateFormat::MDY;
    else if (df == "dmy")
        s.date_format = DateFormat::DMY;
    std::string st = v["status"].str();
    if (st == "online" || st == "idle" || st == "dnd" || st == "invisible")
        s.status = st;
    if (v["self_mute"].isBool())
        s.self_mute = v["self_mute"].asBool();
    if (v["self_deaf"].isBool())
        s.self_deaf = v["self_deaf"].asBool();
    return s;
}

Settings Settings::load()
{
    std::string text;
    if (!config::readText(config::kSettingsPath, text))
        return Settings();
    return fromJson(text);
}

bool Settings::save() const
{
    config::ensureDir();
    return config::writeText(config::kSettingsPath, toJson());
}
