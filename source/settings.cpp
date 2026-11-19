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

std::string Settings::toJson() const
{
    json::Value v = json::Value::object();
    v.set("theme", json::Value::string(theme == Theme::Light ? "light" : theme == Theme::Amoled ? "amoled" : "dark"));
    v.set("mouse", json::Value::boolean(mouse));
    v.set("touch", json::Value::boolean(touch));
    v.set("qr_login", json::Value::boolean(qr_login));
    v.set("videos", json::Value::boolean(videos));
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
