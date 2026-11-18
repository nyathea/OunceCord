// SPDX-License-Identifier: MIT
#include "discord/cdn.h"

#include <cstdlib>

namespace discord::cdn {

static std::string sized(std::string path, int size)
{
    return path + "?size=" + std::to_string(size);
}

std::string avatarPath(const User &u, int size)
{
    if (!u.avatar.empty())
        return sized("/avatars/" + std::to_string(u.id) + "/" + u.avatar + ".png", size);
    // Default avatars: by user ID on the new username system, by
    // discriminator for legacy names.
    int index;
    if (u.discriminator.empty() || u.discriminator == "0")
        index = (int)((u.id >> 22) % 6);
    else
        index = atoi(u.discriminator.c_str()) % 5;
    return "/embed/avatars/" + std::to_string(index) + ".png";
}

std::string guildIconPath(const Guild &g, int size)
{
    if (g.icon.empty())
        return {};
    return sized("/icons/" + std::to_string(g.id) + "/" + g.icon + ".png", size);
}

std::string dmIconPath(const Channel &c, int size)
{
    if (!c.icon.empty())
        return sized("/channel-icons/" + std::to_string(c.id) + "/" + c.icon + ".png", size);
    if (c.type == ChannelType_DM && !c.recipients.empty())
        return avatarPath(c.recipients[0], size);
    return {};
}

}  // namespace discord::cdn
