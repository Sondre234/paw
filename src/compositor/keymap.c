/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Keymaps: the one the keyboard settings describe (keyboard.file, else the XKB names), compiled
 * once and shared by every keyboard but the virtual ones, which bring their own, and the layout
 * active on all of them. */
#include "server.h"

/* The keymap in the file at `path`, read into memory first: xkbcommon maps a file it is handed,
 * and one cut short while it reads (an editor saving it in place, as a reload follows the save)
 * would end the compositor with SIGBUS. At most 4 MiB, as the configuration's check reads. */
static struct xkb_keymap *keymap_from_path(struct xkb_context *context, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file)
        return NULL;
    enum { limit = 4 << 20 };
    char *text = malloc(limit);
    size_t size = text ? fread(text, 1, limit, file) : 0;
    bool whole = text && !ferror(file) && feof(file);
    fclose(file);
    struct xkb_keymap *keymap = NULL;
    if (whole)
        keymap = xkb_keymap_new_from_buffer(context, text, size, XKB_KEYMAP_FORMAT_TEXT_V1,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
    free(text);
    return keymap;
}

/* keyboard.file when it is set and compiles (the configuration checked it, but it may have
 * changed since), else the XKB names, else xkbcommon's defaults: never no keymap at all.
 * `from_file` says which. */
static struct xkb_keymap *compile_keymap(const struct sh_settings *settings, bool *from_file) {
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context)
        return NULL;
    struct xkb_keymap *keymap = NULL;
    *from_file = false;
    if (settings->keyboard_file[0]) {
        keymap = keymap_from_path(context, settings->keyboard_file);
        if (!keymap)
            wlr_log(WLR_ERROR, "Cannot compile the keymap %s; using keyboard.layout instead",
                    settings->keyboard_file);
        *from_file = keymap != NULL;
    }
    struct xkb_rule_names names = {.rules = settings->keyboard_rules,
                                   .layout = settings->keyboard_layout,
                                   .variant = settings->keyboard_variant,
                                   .model = settings->keyboard_model,
                                   .options = settings->keyboard_options};
    if (!keymap)
        keymap = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
        keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    xkb_context_unref(context);
    return keymap;
}

/* The layout of `keymap` that `layout` of `old` becomes: the one of the same name, else the one
 * in the same place, else the first. */
static xkb_layout_index_t carry_layout(struct xkb_keymap *old, xkb_layout_index_t layout,
                                       struct xkb_keymap *keymap) {
    xkb_layout_index_t count = xkb_keymap_num_layouts(keymap);
    const char *name = layout < xkb_keymap_num_layouts(old)
                           ? xkb_keymap_layout_get_name(old, layout)
                           : NULL;
    for (xkb_layout_index_t i = 0; name && i < count; ++i) {
        const char *candidate = xkb_keymap_layout_get_name(keymap, i);
        if (candidate && !strcmp(candidate, name))
            return i;
    }
    return layout < count ? layout : 0;
}

/* The modifiers in `mask` of `old`, as those of the same names in `keymap`. */
static xkb_mod_mask_t carry_modifiers(struct xkb_keymap *old, xkb_mod_mask_t mask,
                                      struct xkb_keymap *keymap) {
    xkb_mod_mask_t carried = 0;
    for (xkb_mod_index_t i = 0; old && i < xkb_keymap_num_mods(old) && i < 32; ++i) {
        if (!(mask & 1u << i))
            continue;
        xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, xkb_keymap_mod_get_name(old, i));
        if (index < 32)
            carried |= 1u << index;
    }
    return carried;
}

/* Gives `keyboard` the shared keymap, in the active layout. The keys it holds stay down (wlroots
 * presses them again in the new keymap), and so do its locks, such as Caps Lock. */
static bool give_keymap(struct sh_server *server, struct wlr_keyboard *keyboard) {
    struct xkb_keymap *old = keyboard->keymap ? xkb_keymap_ref(keyboard->keymap) : NULL;
    xkb_mod_mask_t locked = keyboard->modifiers.locked;
    bool ok = wlr_keyboard_set_keymap(keyboard, server->keymap);
    if (ok)
        wlr_keyboard_notify_modifiers(keyboard, keyboard->modifiers.depressed,
                                      keyboard->modifiers.latched,
                                      carry_modifiers(old, locked, server->keymap),
                                      server->keyboard_layout);
    xkb_keymap_unref(old);
    return ok;
}

/* A keyboard plugged in: the shared keymap, the active layout, the repeat settings. */
bool configure_keyboard(struct sh_server *server, struct wlr_keyboard *keyboard) {
    const struct sh_settings *settings = server_settings(server);
    if (!server->keymap || !give_keymap(server, keyboard))
        return false;
    wlr_keyboard_set_repeat_info(keyboard, settings->repeat_rate, settings->repeat_delay);
    return true;
}

/* At startup and after a reload: compiles the keymap the settings describe and, when it is not
 * the one in use, gives it to every keyboard but the virtual ones. The active layout becomes the
 * new keymap's of the same name, else the one in its place, else the first. A reload that leaves
 * the keymap as it was leaves the keyboards alone, but for the repeat settings. */
void update_keymap(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    bool from_file;
    struct xkb_keymap *keymap = compile_keymap(settings, &from_file);
    struct sh_keyboard *keyboard;
    if (keymap)
        server->keymap_from_file = from_file;
    if (!keymap) {
        wlr_log(WLR_ERROR, "Cannot compile any keymap; keeping the one in use");
    } else if (server->keymap && wlr_keyboard_keymaps_match(server->keymap, keymap)) {
        xkb_keymap_unref(keymap);
    } else {
        struct xkb_keymap *old = server->keymap;
        server->keyboard_layout = old ? carry_layout(old, server->keyboard_layout, keymap) : 0;
        server->keymap = keymap;
        server->syncing_keyboards = true;
        wl_list_for_each(keyboard, &server->keyboards, link) {
            if (!keyboard->is_virtual && !give_keymap(server, keyboard->wlr_keyboard))
                wlr_log(WLR_ERROR, "Could not apply reloaded keymap");
        }
        server->syncing_keyboards = false;
        // Applications got the new keymap from the seat's keyboard; the focused one also needs
        // the modifiers and layout that came with it.
        struct wlr_keyboard *seat_keyboard = wlr_seat_get_keyboard(server->seat);
        if (seat_keyboard && seat_keyboard->keymap == server->keymap)
            wlr_seat_keyboard_notify_modifiers(server->seat, &seat_keyboard->modifiers);
        if (old)
            xkb_keymap_unref(old);
    }
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (!keyboard->is_virtual)
            wlr_keyboard_set_repeat_info(keyboard->wlr_keyboard, settings->repeat_rate,
                                         settings->repeat_delay);
    }
    if (server->running)
        notify_subscribers(server); // the layouts, or their short names, may have changed
}

/* Makes `layout` (from 0) the one every keyboard but the virtual ones types in. `source` is a
 * keyboard that switched to it itself and has told the seat, or NULL. */
static void set_keyboard_layout(struct sh_server *server, xkb_layout_index_t layout,
                                struct sh_keyboard *source) {
    server->keyboard_layout = layout;
    server->syncing_keyboards = true;
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
        if (keyboard == source || keyboard->is_virtual || !wlr_keyboard->xkb_state)
            continue;
        wlr_keyboard_notify_modifiers(wlr_keyboard, wlr_keyboard->modifiers.depressed,
                                      wlr_keyboard->modifiers.latched,
                                      wlr_keyboard->modifiers.locked, layout);
    }
    server->syncing_keyboards = false;
    struct wlr_keyboard *seat_keyboard = wlr_seat_get_keyboard(server->seat);
    if (!source && seat_keyboard && seat_keyboard->keymap == server->keymap)
        wlr_seat_keyboard_notify_modifiers(server->seat, &seat_keyboard->modifiers);
    notify_subscribers(server); // the panel's layout indicator
}

/* After a keyboard's modifiers changed: when it switched layout itself, by an XKB option such
 * as grp:alt_shift_toggle, the other keyboards follow. A layout held only while a key is
 * (grp:switch) is not a switch. */
void follow_keyboard_layout(struct sh_server *server, struct sh_keyboard *keyboard) {
    struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
    if (keyboard->is_virtual || !wlr_keyboard->xkb_state || wlr_keyboard->keymap != server->keymap)
        return;
    xkb_layout_index_t layout =
        xkb_state_serialize_layout(wlr_keyboard->xkb_state, XKB_STATE_LAYOUT_LOCKED);
    if (layout != server->keyboard_layout)
        set_keyboard_layout(server, layout, keyboard);
}

/* switch_layout: `choice` 0 is the next layout and -1 the previous, both wrapping, and N > 0
 * the Nth; one past the last does nothing. */
void switch_keyboard_layout(struct sh_server *server, int choice) {
    xkb_layout_index_t count = server->keymap ? xkb_keymap_num_layouts(server->keymap) : 0;
    if (!count || (choice > 0 && (xkb_layout_index_t)choice > count))
        return;
    xkb_layout_index_t step = choice < 0 ? count - 1 : 1;
    set_keyboard_layout(server,
                        choice > 0 ? (xkb_layout_index_t)choice - 1
                                   : (server->keyboard_layout + step) % count,
                        NULL);
}

/* What the panel shows for `layout` (from 0): its code in keyboard.layout ("us"), or for a
 * keymap file, which has no codes, the first two letters of its name ("Norwegian": "no"); its
 * number when neither gives one. Only letters, digits, '-' and '_'. */
void layout_short_name(struct sh_server *server, xkb_layout_index_t layout, char *name,
                       size_t size) {
    const char *source = NULL;
    size_t length = 0;
    if (!server->keymap_from_file) {
        source = server_settings(server)->keyboard_layout;
        for (xkb_layout_index_t i = 0; i < layout && source; ++i) {
            source = strchr(source, ',');
            source = source ? source + 1 : NULL;
        }
        if (source)
            length = strcspn(source, ",(");
    }
    if (!length && server->keymap && layout < xkb_keymap_num_layouts(server->keymap)) {
        source = xkb_keymap_layout_get_name(server->keymap, layout);
        length = source ? strnlen(source, 2) : 0;
    }
    size_t used = 0;
    for (size_t i = 0; i < length && used + 1 < size; ++i) {
        char c = source[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')
            name[used++] = c;
    }
    name[used] = '\0';
    if (!used)
        snprintf(name, size, "%u", layout + 1);
}
