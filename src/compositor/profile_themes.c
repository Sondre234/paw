/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cursor and icon themes of appearance profiles (appearance.cursor_theme, cursor_size and
 * icon_theme). The cursor is drawn in the theme and size the profile names, the theme where it is
 * installed, else in the session's own: $XCURSOR_THEME and $XCURSOR_SIZE as paw started. A change
 * makes a new xcursor manager, gives the cursor its image from it and X11 windows their arrow,
 * programs started afterwards $XCURSOR_THEME and $XCURSOR_SIZE, and subscribers "cursor-theme SIZE
 * NAME". A login session also tells applications through gsettings' org.gnome.desktop.interface
 * cursor-theme, cursor-size and icon-theme, keeping each value it replaces to put back as a
 * profile without one is picked and as the session ends. The shell draws its icons in the icon
 * theme itself (shell/icons.cpp). `get cursor_theme`. */
#include "server.h"
#include <spawn.h>

/* A key of org.gnome.desktop.interface a login session sets: the value it set last, "" for none,
 * and the one it replaced, as `gsettings get` printed it, which goes back as no profile names
 * one. */
struct desktop_key {
    const char *name;
    char told[128];
    char replaced[256];
    bool saved;       /* `replaced` holds the value to put back */
    bool unavailable; /* no gsettings, or no such key: nothing is set */
};

enum { KEY_CURSOR_THEME, KEY_CURSOR_SIZE, KEY_ICON_THEME, KEY_COUNT };

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
    themes->keys[KEY_CURSOR_THEME].name = "cursor-theme";
    themes->keys[KEY_CURSOR_SIZE].name = "cursor-size";
    themes->keys[KEY_ICON_THEME].name = "icon-theme";
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

/* Runs gsettings with `words` after its name; its pid, or 0 when it cannot start. */
static pid_t run_gsettings(const char *const words[3]) {
    const char *argv[] = {"gsettings", "set", "org.gnome.desktop.interface",
                          words[0], words[1], NULL};
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
        wlr_log(WLR_ERROR, "Cannot run gsettings to set %s: %s", words[0], strerror(error));
        return 0;
    }
    return pid;
}

/* What `gsettings get org.gnome.desktop.interface KEY` prints, without its newline; false
 * without gsettings or the key. Waited for: it reads dconf's database, without D-Bus. */
static bool read_key(const char *key, char *value, size_t size) {
    char command[128];
    snprintf(command, sizeof(command), "gsettings get org.gnome.desktop.interface %s 2>/dev/null",
             key);
    FILE *output = popen(command, "r");
    if (!output)
        return false;
    bool read = fgets(value, (int)size, output) != NULL;
    if (pclose(output) != 0 || !read)
        return false;
    value[strcspn(value, "\n")] = '\0';
    return *value != '\0';
}

/* The user's icon theme, from a value of icon-theme as gsettings printed it ('Name'), for a shell
 * started while a profile's is set in its place (PAW_USER_ICON_THEME, shell/icons.cpp). */
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
static void tell(struct desktop_key *key, const char *value) {
    if (key->unavailable || !strcmp(key->told, value))
        return;
    if (*value) {
        if (!key->saved && !read_key(key->name, key->replaced, sizeof(key->replaced))) {
            key->unavailable = true;
            return;
        }
        if (!key->saved && !strcmp(key->name, "icon-theme"))
            keep_user_icon_theme(key->replaced);
        key->saved = true;
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
        run_gsettings((const char *const[]){key->name, text, NULL});
    } else if (key->saved) {
        run_gsettings((const char *const[]){key->name, key->replaced, NULL});
        key->saved = false;
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
    tell(&themes->keys[KEY_CURSOR_THEME], installed ? named : "");
    tell(&themes->keys[KEY_CURSOR_SIZE], size_text);
    // The shell says when an icon theme is not installed.
    const char *icons = settings->icon_theme;
    tell(&themes->keys[KEY_ICON_THEME], *icons && sh_theme_installed(icons, false) ? icons : "");
}

/* As the session ends: the values it replaced go back, waited for a second at most, so that the
 * next session starts from the user's own. */
void profile_themes_finish(struct sh_server *server) {
    struct sh_profile_themes *themes = server->profile_themes;
    if (!themes)
        return;
    pid_t running[KEY_COUNT];
    int count = 0;
    for (int i = 0; i < KEY_COUNT; ++i) {
        struct desktop_key *key = &themes->keys[i];
        pid_t pid = key->saved ? run_gsettings((const char *const[]){key->name, key->replaced,
                                                                     NULL})
                               : 0;
        if (pid > 0)
            running[count++] = pid;
    }
    for (int tries = 0; count > 0 && tries < 100; ++tries) {
        for (int i = 0; i < count;) {
            if (waitpid(running[i], NULL, WNOHANG) != 0)
                running[i] = running[--count];
            else
                ++i;
        }
        if (count)
            usleep(10000);
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
