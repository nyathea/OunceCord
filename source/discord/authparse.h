// SPDX-License-Identifier: MIT
//
// Interpreting responses from Discord's login endpoints. Kept free of
// platform code so it can be tested on a desktop.
#pragma once

#include <string>

#include "util/json.h"

namespace discord {

struct AuthResult {
    enum class Kind {
        Token,    // signed in; token is set
        Mfa,      // a second factor is needed; see the mfa_* fields
        SmsSent,  // a code was texted to phone
        Captcha,  // Discord wants a captcha, which we cannot show
        Error,    // message says why
    };
    Kind kind = Kind::Error;
    std::string token;
    std::string message;
    std::string phone;  // SmsSent: the redacted number
    bool mfa_totp = false, mfa_sms = false, mfa_backup = false, mfa_webauthn = false;
    bool ticket_expired = false;  // the MFA ticket is no longer usable: log in again
};

// The first human-readable error in a Discord error body: a field error
// ("Login or password is invalid."), else the top-level message.
std::string authErrorMessage(int status, const json::Value &body);

// Reads the response to /auth/login or /auth/mfa/<type>. On an MFA answer,
// ticket and login_instance_id receive the values to send back. On a 429,
// retry_after_s receives the wait.
AuthResult parseAuthResponse(int status, const std::string &body, std::string &ticket,
                             std::string &login_instance_id, double &retry_after_s);

}  // namespace discord
