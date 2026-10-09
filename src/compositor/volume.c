/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The keyboard's volume, microphone and brightness keys (volume_up, volume_down, volume_mute,
 * mic_mute, brightness_up, brightness_down): the shell carries them out on the sound server and
 * the backlight, and shows them on its on-screen display; while no shell listens, wpctl and
 * brightnessctl do. */
#include "server.h"

/* Starts a program with an ordinary signal mask (the compositor blocks signals for its event
 * loop); the child is reaped with the others. */
static bool run_program(const char *const argv[]) {
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
        return false;
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t pid;
    extern char **environ;
    int error = posix_spawnp(&pid, argv[0], NULL, &attributes, (char *const *)argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (error)
        wlr_log(WLR_ERROR, "No shell to change the volume or brightness, and cannot launch %s: %s",
                argv[0], strerror(error));
    return error == 0;
}

/* Without a shell: wpctl (WirePlumber's) for the default output and input, unmuting the output
 * as its volume changes, as the shell does, and keeping it at most 100 %; brightnessctl for the
 * backlight, which it keeps from going fully dark. */
static void run_fallback(enum sh_action action, int percent) {
    char step[16];
    bool up = action == SH_VOLUME_UP || action == SH_BRIGHTNESS_UP;
    snprintf(step, sizeof(step), "%d%%%s", percent, up ? "+" : "-");
    switch (action) {
    case SH_VOLUME_UP:
    case SH_VOLUME_DOWN: {
        const char *unmute[] = {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "0", NULL};
        const char *volume[] = {"wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@",
                                step, NULL};
        if (run_program(unmute))
            run_program(volume);
        break;
    }
    case SH_VOLUME_MUTE:
    case SH_MIC_MUTE: {
        const char *mute[] = {"wpctl", "set-mute",
                              action == SH_MIC_MUTE ? "@DEFAULT_AUDIO_SOURCE@"
                                                    : "@DEFAULT_AUDIO_SINK@",
                              "toggle", NULL};
        run_program(mute);
        break;
    }
    case SH_BRIGHTNESS_UP:
    case SH_BRIGHTNESS_DOWN: {
        const char *brightness[] = {"brightnessctl", "--quiet", "--min-value=1", "set", step, NULL};
        run_program(brightness);
        break;
    }
    default:
        break;
    }
}

/* Tells the shell, and every other subscriber: "volume up|down PERCENT", "volume mute",
 * "microphone mute", "brightness up|down PERCENT". */
void volume_action(struct sh_server *server, enum sh_action action, int percent) {
    char line[64];
    switch (action) {
    case SH_VOLUME_UP:
    case SH_VOLUME_DOWN:
        snprintf(line, sizeof(line), "volume %s %d\n", action == SH_VOLUME_UP ? "up" : "down",
                 percent);
        break;
    case SH_VOLUME_MUTE:
        snprintf(line, sizeof(line), "volume mute\n");
        break;
    case SH_MIC_MUTE:
        snprintf(line, sizeof(line), "microphone mute\n");
        break;
    case SH_BRIGHTNESS_UP:
    case SH_BRIGHTNESS_DOWN:
        snprintf(line, sizeof(line), "brightness %s %d\n",
                 action == SH_BRIGHTNESS_UP ? "up" : "down", percent);
        break;
    default:
        return;
    }
    if (!shell_listening(server))
        run_fallback(action, percent);
    send_shell_line(server, line);
}
