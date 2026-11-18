// SPDX-License-Identifier: MIT
//
// OunceCord: a Discord client for Switch 2 (kaiHEN) and Switch 1.
#include <cstdio>

#include <switch.h>

#include "config.h"
#include "discord/rest.h"
#include "net/tls.h"
#include "ui/app.h"
#include "util/util.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    config::ensureDir();
    logInit(config::kLogPath);
    logf("OunceCord starting");

    // Mesa logs to stderr; keep it with ours.
    freopen("sdmc:/switch/OunceCord/mesa.txt", "w", stderr);

    timeInitialize();
    if (!netInitialize())
        logf("network init failed; continuing offline");
    discord::Identity::get().init();

    // Stay awake while the app is open: chat should not drop on auto-sleep.
    appletSetAutoSleepDisabled(true);

    App app;
    if (!app.init()) {
        logf("app init failed");
        netExit();
        timeExit();
        logExit();
        return 1;
    }
    while (appletMainLoop() && app.frame()) {
    }
    app.exit();

    appletSetAutoSleepDisabled(false);
    netExit();
    timeExit();
    logf("OunceCord exiting");
    logExit();
    return 0;
}
