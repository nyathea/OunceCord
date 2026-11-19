// SPDX-License-Identifier: MIT
#include "discord/authparse.h"

#include <cstdio>

namespace discord {

// Depth-first search for {"_errors": [{"message": ...}]}.
static const json::Value *firstFieldError(const json::Value &v, int depth = 0)
{
    if (!v.isObject() || depth > 8)
        return nullptr;
    const json::Value &errs = v["_errors"];
    if (errs.isArray() && errs[(size_t)0]["message"].isString())
        return &errs[(size_t)0];
    for (const auto &m : v.members())
        if (const json::Value *e = firstFieldError(m.second, depth + 1))
            return e;
    return nullptr;
}

std::string authErrorMessage(int status, const json::Value &body)
{
    if (const json::Value *e = firstFieldError(body["errors"]))
        return (*e)["message"].asString();
    if (body["message"].isString() && !body["message"].asString().empty())
        return body["message"].asString();
    if (status == 0)
        return "Could not reach Discord. Check the internet connection.";
    char buf[64];
    snprintf(buf, sizeof(buf), "Discord answered with HTTP %d.", status);
    return buf;
}

AuthResult parseAuthResponse(int status, const std::string &body, std::string &ticket,
                             std::string &login_instance_id, double &retry_after_s)
{
    AuthResult r;
    json::Value v;
    bool parsed = !body.empty() && json::parse(body, v) && v.isObject();

    if (status == 429) {
        retry_after_s = parsed ? v["retry_after"].asDouble(5) : 5;
        if (retry_after_s < 1)
            retry_after_s = 1;
        char buf[96];
        snprintf(buf, sizeof(buf), "Too many attempts. Try again in %d seconds.",
                 (int)(retry_after_s + 0.999));
        r.message = buf;
        return r;
    }

    if (parsed && v["captcha_key"].isArray()) {
        r.kind = AuthResult::Kind::Captcha;
        r.message = "Discord asked for a captcha, which OunceCord can't show.";
        return r;
    }

    if (status == 200 && parsed) {
        if (v["token"].isString() && !v["token"].asString().empty()) {
            r.kind = AuthResult::Kind::Token;
            r.token = v["token"].asString();
            return r;
        }
        if (v["mfa"].asBool() && v["ticket"].isString()) {
            r.kind = AuthResult::Kind::Mfa;
            ticket = v["ticket"].asString();
            login_instance_id = v["login_instance_id"].str();
            r.mfa_totp = v["totp"].asBool();
            r.mfa_sms = v["sms"].asBool();
            r.mfa_backup = v["backup"].asBool();
            r.mfa_webauthn = v["webauthn"].isString();
            return r;
        }
        if (v["required_actions"].isArray()) {
            r.message = "Discord needs you to take care of your account (for example, reset the "
                        "password) in the official app first.";
            return r;
        }
        r.message = "Discord sent an unexpected answer. Try again.";
        return r;
    }

    r.message = authErrorMessage(status, parsed ? v : json::Value());
    // 60006 / 60008 and friends: the ticket timed out or was used up.
    int64_t code = parsed ? v["code"].asInt() : 0;
    if (code == 60006 || (r.message.find("ticket") != std::string::npos))
        r.ticket_expired = true;
    return r;
}

}  // namespace discord
