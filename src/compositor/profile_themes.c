/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cursor and icon themes of appearance profiles (appearance.cursor_theme, cursor_size and
 * icon_theme). The cursor is drawn in the theme and size the profile names, the theme where it is
 * installed, else in the session's own: $XCURSOR_THEME and $XCURSOR_SIZE as paw started. A change
 * makes a new xcursor manager, gives the cursor its image from it and X11 windows their arrow,
 * programs started afterwards $XCURSOR_THEME and $XCURSOR_SIZE, and subscribers "cursor-theme SIZE
 * NAME". A login session also tells applications through gsettings' org.gnome.desktop.interface
 * cursor-theme, cursor-size and icon-theme, keeping each value it replaces to put back as a
 * profile without one is picked and as the session ends; a key that held only its default is
 * reset instead. Until they are back they wait on disk, so that a session ending without putting
 * them back (killed, or the machine stopping first) has the next put them back as it starts
 * (sh_profile_themes_recover). The shell draws its icons in the icon theme itself
 * (shell/icons.cpp). `get cursor_theme`. */
#include "server.h"
#include <limits.h>
#include <spawn.h>

/* A key of org.gnome.desktop.interface a login session sets: the value it set last, "" for none,
 * and the one it replaced, as `gsettings get` printed it, "" for the key's default, which goes
 * back as no profile names one. */
struct desktop_key {
    const char *name;
    char told[128];
    char replaced[256];
    bool saved;       /* `replaced` holds the value to put back */
    bool unavailable; /* no gsettings, or no such key: nothing is set */
};

enum { KEY_CURSOR_THEME, KEY_CURSOR_SIZE, KEY_ICON_THEME, KEY_COUNT };
static const char *const key_names[KEY_COUNT] = {"cursor-theme", "cursor-size", "icon-theme"};

struct sh_profile_themes {
    char base_theme[128]; /* the session's own cursor theme and size */
    int base_size;
    char theme[128]; /* the cursor theme and size drawn */
    int size;
    char missing[128]; /* the cursor theme named last that is not installed, said once */
    struct desktop_key keys[KEY_COUNT];
};

void profile_themes_init(struct sh_server *server) {
    struct sh_profile_themes *themes = calloc(1, sizeof(*themes));
    if (!themes) { // the cursor in Adwaita, as no profile can change it
        server->cursor_mgr = wlr_xcursor_manager_create("Adwaita", 24);
        return;
    }
    server->profile_themes = themes;
    /* Without a theme, wlroots draws the bare X11 arrow. Adwaita ships with GTK, so it is
     * nearly always installed; $XCURSOR_THEME and $XCURSOR_SIZE still win, and main.cpp sets them
     * from the desktop's settings when they are not set. Exporting both lets applications draw
     * the same cursor over their windows. */
    const char *theme = getenv("XCURSOR_THEME");
    snprintf(themes->base_theme, sizeof(themes->base_theme), "%s",
             theme && *theme && strlen(theme) < sizeof(themes->base_theme) ? theme : "Adwaita");
    const char *size = getenv("XCURSOR_SIZE");
    themes->base_size = size && atoi(size) > 0 && atoi(size) <= 256 ? atoi(size) : 24;
    for (int i = 0; i < KEY_COUNT; ++i)
        themes->keys[i].name = key_names[i];
    profile_themes_reload(server);
}

const char *cursor_theme_name(struct sh_server *server) {
    return server->profile_themes ? server->profile_themes->theme : "Adwaita";
}

int cursor_theme_size(struct sh_server *server) {
    return server->profile_themes && server->profile_themes->size > 0
               ? server->profile_themes->size
               : 24;
}

/* Draws the cursor in `theme` at `size` pixels: a manager for them in place of the old one, the
 * image the cursor shows taken from it again, a cursor grown by a shake back to its size, and X11
 * windows given its arrow. */
static void use_cursor_theme(struct sh_server *server, const char *theme, int size) {
    struct sh_profile_themes *themes = server->profile_themes;
    if (server->cursor_mgr && !strcmp(themes->theme, theme) && themes->size == size)
        return;
    struct wlr_xcursor_manager *manager = wlr_xcursor_manager_create(theme, (uint32_t)size);
    if (!manager) {
        wlr_log(WLR_ERROR, "Cannot load the cursor theme %s", theme);
        return;
    }
    struct wlr_xcursor_manager *old = server->cursor_mgr;
    server->cursor_mgr = manager;
    snprintf(themes->theme, sizeof(themes->theme), "%s", theme);
    themes->size = size;
    char size_text[16];
    snprintf(size_text, sizeof(size_text), "%d", size);
    setenv("XCURSOR_THEME", theme, true);
    setenv("XCURSOR_SIZE", size_text, true);
    if (!old)
        return; // as paw starts, before anything shows
    wlr_log(WLR_INFO, "The cursor is drawn in %s at %d pixels", theme, size);
    cursor_shake_retheme(server);
    // The cursor shows an image of the old manager's only when it shows an xcursor by name:
    // hidden, grown or a client's own, it holds none.
    const char *image = server->cursor_image;
    if (image && strcmp(image, "client"))
        show_xcursor(server, image);
    wlr_xcursor_manager_destroy(old);
#if WLR_HAS_XWAYLAND
    xwayland_give_cursor(server);
#endif
    notify_subscribers(server);
}

/* Runs `gsettings COMMAND org.gnome.desktop.interface KEY VALUE`, without VALUE when it is NULL;
 * its pid, or 0 when it cannot start. */
static pid_t run_gsettings(const char *command, const char *key, const char *value) {
    const char *argv[] = {"gsettings", command, "org.gnome.desktop.interface", key, value, NULL};
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes))
        return 0;
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t pid = 0;
    extern char **environ;
    int error = posix_spawnp(&pid, argv[0], NULL, &attributes, (char *const *)argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (error) {
        wlr_log(WLR_ERROR, "Cannot run gsettings to %s %s: %s", command, key, strerror(error));
        return 0;
    }
    return pid;
}

/* Puts back `value` of `key` as gsettings printed it, "" by a reset. */
static pid_t put_back(const char *key, const char *value) {
    return *value ? run_gsettings("set", key, value) : run_gsettings("reset", key, NULL);
}

/* Waits a second at most for the gsettings putting values back; whether every one of them
 * finished, and did so without an error. */
static bool all_back(pid_t *running, int count) {
    bool back = true;
    for (int tries = 0; count > 0 && tries < 100; ++tries) {
        for (int i = 0; i < count;) {
            int status = 0;
            pid_t ended = waitpid(running[i], &status, WNOHANG);
            if (ended == 0) {
                ++i;
                continue;
            }
            back = back && ended > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
            running[i] = running[--count];
        }
        if (count)
            usleep(10000);
    }
    return back && count == 0;
}

/* $XDG_STATE_HOME/paw/replaced-settings: the values a login session replaced and has yet to put
 * back, a line for each key, its name and the value as gsettings printed it, or its name alone
 * for one to reset. */
static bool record_path(char *path, size_t size) {
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int length;
    if (state && state[0] == '/')
        length = snprintf(path, size, "%s/paw/replaced-settings", state);
    else if (home && home[0] == '/')
        length = snprintf(path, size, "%s/.local/state/paw/replaced-settings", home);
    else
        return false;
    return length > 0 && (size_t)length < size;
}

/* Writes the record anew as a value is replaced or goes back, before gsettings changes it, and
 * removes it once none is left to put back. */
static void keep_record(const struct sh_profile_themes *themes) {
    char path[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 8];
    if (!record_path(path, sizeof(path)))
        return;
    bool any = false;
    for (int i = 0; i < KEY_COUNT; ++i)
        any = any || themes->keys[i].saved;
    if (!any) {
        if (unlink(path) != 0 && errno != ENOENT)
            wlr_log_errno(WLR_ERROR, "Cannot remove %s", path);
        return;
    }
    snprintf(directory, sizeof(directory), "%s", path);
    *strrchr(directory, '/') = '\0';
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *file = make_directories(directory) ? fopen(temporary, "w") : NULL;
    if (file) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            const struct desktop_key *key = &themes->keys[i];
            if (key->saved)
                fprintf(file, "%s%s%s\n", key->name, *key->replaced ? " " : "", key->replaced);
        }
        if (fclose(file) == 0 && rename(temporary, path) == 0)
            return;
    }
    wlr_log_errno(WLR_ERROR, "Cannot write %s, which keeps the desktop settings to put back", path);
}

/* The record left by a session that never reached profile_themes_finish, put back before the
 * new session reads anything from gsettings, and gone once it is back. */
bool sh_profile_themes_recover(void) {
    char path[PATH_MAX];
    if (!record_path(path, sizeof(path)))
        return false;
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT)
            wlr_log_errno(WLR_ERROR, "Cannot read %s", path);
        return false;
    }
    pid_t running[KEY_COUNT];
    int count = 0;
    bool started = true;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        char *value = strchr(line, ' ');
        if (value)
            *value++ = '\0';
        int known = 0;
        while (known < KEY_COUNT && strcmp(line, key_names[known]))
            ++known;
        if (known == KEY_COUNT || count == KEY_COUNT) {
            wlr_log(WLR_ERROR, "Skipping \"%s\" in %s, not a setting paw replaces", line, path);
            continue;
        }
        pid_t pid = put_back(key_names[known], value ? value : "");
        if (pid > 0)
            running[count++] = pid;
        started = started && pid > 0;
    }
    fclose(file);
    if (!all_back(running, count) || !started) {
        wlr_log(WLR_ERROR, "Cannot put back every desktop setting %s holds, which the last "
                           "session replaced and left", path);
        return false;
    }
    if (unlink(path) != 0)
        wlr_log_errno(WLR_ERROR, "Cannot remove %s", path);
    return true;
}

/* What `gsettings get org.gnome.desktop.interface KEY` prints, without its newline, or "" when
 * that is the key's default, which gsettings' memory backend holds alone: putting back a key
 * the user never set resets it, rather than setting the default, which would hide GTK's
 * settings.ini from the shell. A value the user set to the default counts as none, which a
 * reset gives back as well. False without gsettings or the key. Waited for: it reads dconf's
 * database, without D-Bus. */
static bool read_key(const char *key, char *value, size_t size) {
    char command[256], fallback[256];
    snprintf(command, sizeof(command),
             "gsettings get org.gnome.desktop.interface %s 2>/dev/null && "
             "GSETTINGS_BACKEND=memory gsettings get org.gnome.desktop.interface %s 2>/dev/null",
             key, key);
    FILE *output = popen(command, "r");
    if (!output)
        return false;
    bool read = fgets(value, (int)size, output) && fgets(fallback, sizeof(fallback), output);
    if (pclose(output) != 0 || !read)
        return false;
    value[strcspn(value, "\n")] = '\0';
    fallback[strcspn(fallback, "\n")] = '\0';
    if (!strcmp(value, fallback))
        *value = '\0';
    return true;
}

/* The user's icon theme, from a value of icon-theme as gsettings printed it ('Name'), empty for
 * none of their own, for a shell started while a profile's is set in its place
 * (PAW_USER_ICON_THEME, shell/icons.cpp). */
static void keep_user_icon_theme(const char *printed) {
    char theme[256];
    size_t length = strlen(printed);
    if (length >= 2 && printed[0] == '\'' && printed[length - 1] == '\'')
        snprintf(theme, sizeof(theme), "%.*s", (int)length - 2, printed + 1);
    else
        snprintf(theme, sizeof(theme), "%s", printed);
    setenv("PAW_USER_ICON_THEME", theme, true);
}

/* Tells applications `value` for `key`, "" putting back the value the session replaced, when it
 * changed: a choice made meanwhile in other settings stands until then. */
static void tell(struct sh_profile_themes *themes, struct desktop_key *key, const char *value) {
    if (key->unavailable || !strcmp(key->told, value))
        return;
    if (*value) {
        if (!key->saved) {
            if (!read_key(key->name, key->replaced, sizeof(key->replaced))) {
                key->unavailable = true;
                return;
            }
            if (!strcmp(key->name, "icon-theme"))
                keep_user_icon_theme(key->replaced);
            key->saved = true;
            keep_record(themes);
        }
        // A GVariant string, quoted, but for the size, a number.
        char text[sizeof(key->told) * 2 + 3];
        size_t length = 0;
        bool quote = strcmp(key->name, "cursor-size") != 0;
        if (quote)
            text[length++] = '\'';
        for (const char *c = value; *c && length < sizeof(text) - 3; ++c) {
            if (quote && (*c == '\'' || *c == '\\'))
                text[length++] = '\\';
            text[length++] = *c;
        }
        if (quote)
            text[length++] = '\'';
        text[length] = '\0';
        run_gsettings("set", key->name, text);
    } else if (key->saved) {
        put_back(key->name, key->replaced);
        key->saved = false;
        keep_record(themes);
        if (!strcmp(key->name, "icon-theme"))
            unsetenv("PAW_USER_ICON_THEME");
    }
    snprintf(key->told, sizeof(key->told), "%s", value);
}

/* After a reload, and as the session starts: the cursor in the profile's theme and size, and in
 * a login session applications told of them and of its icon theme. */
void profile_themes_reload(struct sh_server *server) {
    struct sh_profile_themes *themes = server->profile_themes;
    if (!themes)
        return;
    const struct sh_settings *settings = server_settings(server);
    const char *named = settings->cursor_theme;
    bool installed = *named && sh_theme_installed(named, true);
    if (*named && !installed && strcmp(themes->missing, named))
        wlr_log(WLR_ERROR, "The cursor theme %s is not installed; skipping it for %s", named,
                themes->base_theme);
    snprintf(themes->missing, sizeof(themes->missing), "%s", installed ? "" : named);
    int size = settings->cursor_size > 0 ? settings->cursor_size : themes->base_size;
    use_cursor_theme(server, installed ? named : themes->base_theme, size);
    if (!server->login_session)
        return;
    char size_text[16] = "";
    if (settings->cursor_size > 0)
        snprintf(size_text, sizeof(size_text), "%d", settings->cursor_size);
    tell(themes, &themes->keys[KEY_CURSOR_THEME], installed ? named : "");
    tell(themes, &themes->keys[KEY_CURSOR_SIZE], size_text);
    // The shell says when an icon theme is not installed.
    const char *icons = settings->icon_theme;
    tell(themes, &themes->keys[KEY_ICON_THEME],
         *icons && sh_theme_installed(icons, false) ? icons : "");
}

/* As the session ends: the values it replaced go back, waited for a second at most, so that the
 * next session starts from the user's own; the record goes with them, or stays for the next
 * session to put back what did not go back. A session that replaced nothing leaves the record
 * alone, as a nested paw beside a login session's. */
void profile_themes_finish(struct sh_server *server) {
    struct sh_profile_themes *themes = server->profile_themes;
    if (!themes)
        return;
    pid_t running[KEY_COUNT];
    int count = 0, saved = 0;
    for (int i = 0; i < KEY_COUNT; ++i) {
        struct desktop_key *key = &themes->keys[i];
        pid_t pid = key->saved ? put_back(key->name, key->replaced) : 0;
        saved += key->saved;
        if (pid > 0)
            running[count++] = pid;
    }
    if (saved && all_back(running, count) && count == saved) {
        for (int i = 0; i < KEY_COUNT; ++i)
            themes->keys[i].saved = false;
        keep_record(themes);
    } else if (saved) {
        wlr_log(WLR_ERROR, "Cannot put back every desktop setting the session replaced; the "
                           "next one does");
    }
    free(themes);
    server->profile_themes = NULL;
}

/* `get cursor_theme`: the cursor theme drawn and its size, by a tab. */
void describe_cursor_theme(struct sh_server *server, int fd, const char *arguments) {
    char line[192];
    snprintf(line, sizeof(line), "ok\n%s\t%d\n", cursor_theme_name(server),
             cursor_theme_size(server));
    control_reply(fd, line);
}
