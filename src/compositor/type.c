/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Typing text into what has the keyboard, as wtype does through a virtual keyboard: each
 * character's keysym on a key of a keymap made for the text, pressed and released on a keyboard
 * of the compositor's own, then the seat's own keyboard and keymap back. The shell's emoji picker
 * types what it picks so, and `paw msg type TEXT` does it for anyone. */
#include "server.h"
#include <wlr/interfaces/wlr_keyboard.h>

/* Characters on one keymap at most: text with more different ones is typed a keymap at a time. */
#define TYPE_KEYS 200

/* The next code point of UTF-8 `text` at `*at`, moving past it; -1 for a malformed one. */
static int32_t next_code_point(const unsigned char *text, size_t *at) {
    const unsigned char first = text[(*at)++];
    if (first < 0x80)
        return first;
    int length = first >= 0xf0 && first < 0xf8 ? 4 : first >= 0xe0 ? 3 : first >= 0xc0 ? 2 : 0;
    if (!length)
        return -1;
    int32_t value = first & (0x7f >> length);
    for (int i = 1; i < length; ++i) {
        const unsigned char next = text[*at];
        if ((next & 0xc0) != 0x80)
            return -1;
        value = value << 6 | (next & 0x3f);
        ++*at;
    }
    // Overlong forms, surrogates and what is past Unicode are no characters.
    static const int32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
    if (value < least[length] || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return -1;
    return value;
}

/* The keysym that types `code`: Return, Tab and BackSpace for their control characters. */
static xkb_keysym_t keysym_of(int32_t code) {
    switch (code) {
    case '\n':
        return XKB_KEY_Return;
    case '\t':
        return XKB_KEY_Tab;
    case '\b':
        return XKB_KEY_BackSpace;
    }
    return xkb_utf32_to_keysym((uint32_t)code);
}

/* A keymap with `count` keys, the nth (keycode 9 + n) giving keysyms[n]. */
static struct xkb_keymap *keymap_for(struct xkb_context *context, const xkb_keysym_t *keysyms,
                                     int count) {
    size_t size = 512 + (size_t)count * 80;
    char *text = calloc(1, size);
    if (!text)
        return NULL;
    size_t length = (size_t)snprintf(text, size,
                                     "xkb_keymap {\nxkb_keycodes \"paw-type\" {\n"
                                     "minimum = 8;\nmaximum = %d;\n",
                                     9 + count);
    for (int i = 0; i < count; ++i)
        length += (size_t)snprintf(text + length, size - length, "<K%d> = %d;\n", i, 9 + i);
    length += (size_t)snprintf(text + length, size - length,
                               "};\nxkb_types \"paw-type\" { include \"complete\" };\n"
                               "xkb_compatibility \"paw-type\" { include \"complete\" };\n"
                               "xkb_symbols \"paw-type\" {\n");
    for (int i = 0; i < count; ++i) {
        char name[64];
        xkb_keysym_get_name(keysyms[i], name, sizeof(name));
        length += (size_t)snprintf(text + length, size - length, "key <K%d> {[ %s ]};\n", i, name);
    }
    snprintf(text + length, size - length, "};\n};\n");
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_string(context, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    free(text);
    return keymap;
}

static const struct wlr_keyboard_impl type_keyboard_impl = {.name = "paw-type"};

/* Presses and releases `count` keysyms on a keyboard whose keymap has those of `keys`. */
static bool type_keys(struct sh_server *server, struct xkb_context *context,
                      const xkb_keysym_t *keys, int key_count, const xkb_keysym_t *typed, int count) {
    struct xkb_keymap *keymap = keymap_for(context, keys, key_count);
    if (!keymap)
        return false;
    struct wlr_seat *seat = server->seat;
    struct wlr_keyboard *own = wlr_seat_get_keyboard(seat);
    struct wlr_keyboard keyboard;
    wlr_keyboard_init(&keyboard, &type_keyboard_impl, "paw-type");
    wlr_keyboard_set_keymap(&keyboard, keymap);
    xkb_keymap_unref(keymap);
    // The focused client gets the keymap, then the keys with no modifier held.
    wlr_seat_set_keyboard(seat, &keyboard);
    wlr_seat_keyboard_notify_modifiers(seat, &keyboard.modifiers);
    const uint32_t time = (uint32_t)now_ms();
    for (int i = 0; i < count; ++i) {
        int key = 0;
        while (key < key_count && keys[key] != typed[i])
            ++key;
        // Keycode 9 + key in the keymap is evdev's 1 + key.
        wlr_seat_keyboard_notify_key(seat, time, (uint32_t)(1 + key), WL_KEYBOARD_KEY_STATE_PRESSED);
        wlr_seat_keyboard_notify_key(seat, time, (uint32_t)(1 + key), WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    // The seat's own keyboard, its keymap and the modifiers it holds back.
    wlr_seat_set_keyboard(seat, own);
    if (own)
        wlr_seat_keyboard_notify_modifiers(seat, &own->modifiers);
    wlr_keyboard_finish(&keyboard);
    return true;
}

bool type_text(struct sh_server *server, const char *text, char *error, size_t error_size) {
    if (!*text) {
        snprintf(error, error_size, "usage: type TEXT");
        return false;
    }
    if (!server->seat->keyboard_state.focused_surface) {
        snprintf(error, error_size, "nothing has the keyboard");
        return false;
    }
    const size_t length = strlen(text);
    xkb_keysym_t *typed = calloc(length, sizeof(*typed));
    if (!typed) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    int count = 0;
    for (size_t at = 0; at < length;) {
        const int32_t code = next_code_point((const unsigned char *)text, &at);
        const xkb_keysym_t keysym = code < 0 ? XKB_KEY_NoSymbol : keysym_of(code);
        if (keysym == XKB_KEY_NoSymbol) {
            free(typed);
            snprintf(error, error_size, "the text is not UTF-8 that can be typed");
            return false;
        }
        typed[count++] = keysym;
    }
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    bool typed_all = context != NULL;
    // A keymap at a time, of the next TYPE_KEYS different characters.
    for (int start = 0; typed_all && start < count;) {
        xkb_keysym_t keys[TYPE_KEYS];
        int key_count = 0, end = start;
        for (; end < count; ++end) {
            int key = 0;
            while (key < key_count && keys[key] != typed[end])
                ++key;
            if (key == key_count) {
                if (key_count == TYPE_KEYS)
                    break;
                keys[key_count++] = typed[end];
            }
        }
        typed_all = type_keys(server, context, keys, key_count, typed + start, end - start);
        start = end;
    }
    xkb_context_unref(context);
    free(typed);
    if (!typed_all)
        snprintf(error, error_size, "no keymap could be made for the text");
    return typed_all;
}
