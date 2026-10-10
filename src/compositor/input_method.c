/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Input methods (fcitx5, ibus): text-input-unstable-v3 for the applications that take text, and
 * input-method-unstable-v2 for the input method, relayed between them as sway does. The text
 * input of the surface with the keyboard enables and disables the input method and tells it the
 * surrounding text, the content type and where the text cursor is; the input method's preedit,
 * committed text and deletions go back to it. While the input method grabs the keyboard, the keys
 * no binding takes go to it, and it types what it lets through on a virtual keyboard of its own.
 * Its popups (candidate windows) sit beside the text cursor, on the output, above the windows and
 * below the overlays. The session locked, the lock screen gets the keys as typed. */
#include "server.h"

/* A client's text input: the surface it waits to enter once an input method connects, which has
 * the keyboard meanwhile. */
struct sh_text_input {
    struct wl_list link; // sh_input_methods.text_inputs
    struct sh_server *server;
    struct wlr_text_input_v3 *input;
    struct wlr_surface *pending;
    struct wl_listener enable, commit, disable, destroy, pending_destroy;
};

/* An input method's popup surface, drawn in a tree of its own at `x`, `y` in the layout once
 * placed, and the text cursor's rectangle it was last told, in its own coordinates. */
struct sh_input_popup {
    struct wl_list link; // sh_input_methods.popups
    struct sh_server *server;
    struct wlr_input_popup_surface_v2 *popup;
    struct wlr_scene_tree *tree;
    int x, y;
    bool placed;
    struct wlr_box told;
    struct wl_listener map, unmap, commit, destroy;
};

/* Tells the input method the text input's state: surrounding text, why it changed, the content
 * type, as far as the text input uses them, then done. */
static void send_state(struct wlr_input_method_v2 *input_method, struct wlr_text_input_v3 *input) {
    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT)
        wlr_input_method_v2_send_surrounding_text(input_method, input->current.surrounding.text,
                                                  input->current.surrounding.cursor,
                                                  input->current.surrounding.anchor);
    wlr_input_method_v2_send_text_change_cause(input_method, input->current.text_change_cause);
    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE)
        wlr_input_method_v2_send_content_type(input_method, input->current.content_type.hint,
                                              input->current.content_type.purpose);
    wlr_input_method_v2_send_done(input_method);
}

/* Where the surface is in the layout, as the scene draws it; false when it is not drawn. */
struct surface_search {
    struct wlr_surface *surface;
    int x, y;
    bool found;
};
static void find_surface(struct wlr_scene_buffer *buffer, int x, int y, void *data) {
    struct surface_search *search = data;
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
    if (!search->found && scene_surface && scene_surface->surface == search->surface) {
        search->x = x;
        search->y = y;
        search->found = true;
    }
}

/* Where the surface is in the layout: a window's where the window rests, not where an animation
 * has it now, a panel's where it is, anything else where the scene draws it. False when it is
 * not shown. */
static bool surface_position(struct sh_server *server, struct wlr_surface *surface, int *x,
                             int *y) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) != surface || !toplevel->scene_tree)
            continue;
        if (!wlr_scene_node_coords(&toplevel->scene_tree->node, x, y))
            return false;
        struct wlr_box geometry = toplevel_geometry(toplevel); // the surface's offset in it
        *x -= geometry.x;
        *y -= geometry.y;
        return true;
    }
    struct sh_layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (layer->surface->surface == surface)
            return wlr_scene_node_coords(&layer->scene->tree->node, x, y);
    }
    struct surface_search search = {.surface = surface};
    wlr_scene_node_for_each_buffer(&server->scene->tree.node, find_surface, &search);
    *x = search.x;
    *y = search.y;
    return search.found;
}

/* Puts a popup beside the text cursor of the text input the input method is active for: below
 * it, or above it where there is no room below, moved in from the sides of the output, and tells
 * the input method where the cursor is from the popup. Hidden while there is no such text input,
 * or the popup has nothing to show. A popup of the overlays (the shell's search fields) is drawn
 * over them, any other under them. */
static void place_popup(struct sh_input_popup *popup) {
    struct sh_server *server = popup->server;
    struct sh_input_methods *methods = &server->input_methods;
    struct wlr_surface *surface = popup->popup->surface;
    struct sh_text_input *active = methods->active;
    struct wlr_surface *focus = active ? active->input->focused_surface : NULL;
    int surface_x, surface_y;
    if (!surface->mapped || !focus || !surface_position(server, focus, &surface_x, &surface_y)) {
        wlr_scene_node_set_enabled(&popup->tree->node, false);
        popup->placed = false;
        return;
    }
    struct wlr_box cursor = {0};
    if (active->input->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE)
        cursor = active->input->current.cursor_rectangle;
    cursor.x += surface_x;
    cursor.y += surface_y;
    int width = surface->current.width, height = surface->current.height;
    int x = cursor.x, y = cursor.y + cursor.height;
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, cursor.x, cursor.y);
    if (!output) {
        double closest_x, closest_y;
        wlr_output_layout_closest_point(server->output_layout, NULL, cursor.x, cursor.y, &closest_x,
                                        &closest_y);
        output = wlr_output_layout_output_at(server->output_layout, closest_x, closest_y);
    }
    if (output) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output, &box);
        if (x + width > box.x + box.width)
            x = box.x + box.width - width;
        if (x < box.x)
            x = box.x;
        if (y + height > box.y + box.height)
            y = cursor.y - height;
        if (y < box.y)
            y = box.y;
    }
    struct wlr_layer_surface_v1 *layer =
        wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface_get_root_surface(focus));
    struct wlr_scene_tree *parent =
        layer && layer->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
            ? methods->overlay_popup_tree
            : methods->popup_tree;
    if (popup->tree->node.parent != parent)
        wlr_scene_node_reparent(&popup->tree->node, parent);
    wlr_scene_node_set_position(&popup->tree->node, x, y);
    wlr_scene_node_set_enabled(&popup->tree->node, true);
    popup->x = x;
    popup->y = y;
    popup->placed = true;
    struct wlr_box rectangle = {cursor.x - x, cursor.y - y, cursor.width, cursor.height};
    if (!wlr_box_equal(&rectangle, &popup->told)) {
        popup->told = rectangle;
        wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &rectangle);
    }
}

static void place_popups(struct sh_server *server) {
    struct sh_input_popup *popup;
    wl_list_for_each(popup, &server->input_methods.popups, link) place_popup(popup);
}

/* The input method stops serving the text input it is active for. */
static void deactivate(struct sh_server *server) {
    struct sh_input_methods *methods = &server->input_methods;
    struct sh_text_input *active = methods->active;
    if (!active)
        return;
    methods->active = NULL;
    if (methods->input_method) {
        wlr_input_method_v2_send_deactivate(methods->input_method);
        wlr_input_method_v2_send_done(methods->input_method);
    }
    place_popups(server);
}

/* The input method starts serving a text input that enabled itself on the surface with the
 * keyboard. */
static void activate(struct sh_text_input *text_input) {
    struct sh_server *server = text_input->server;
    struct sh_input_methods *methods = &server->input_methods;
    if (!methods->input_method || !text_input->input->focused_surface)
        return;
    if (methods->active && methods->active != text_input)
        deactivate(server);
    methods->active = text_input;
    wlr_input_method_v2_send_activate(methods->input_method);
    send_state(methods->input_method, text_input->input);
    place_popups(server);
}

static void set_pending(struct sh_text_input *text_input, struct wlr_surface *surface) {
    if (text_input->pending)
        wl_list_remove(&text_input->pending_destroy.link);
    text_input->pending = surface;
    if (surface)
        wl_signal_add(&surface->events.destroy, &text_input->pending_destroy);
}

static void pending_destroy(struct wl_listener *listener, void *data) {
    struct sh_text_input *text_input = wl_container_of(listener, text_input, pending_destroy);
    set_pending(text_input, NULL);
}

/* Each text input follows the keyboard: it leaves the surface that had it, and the text inputs
 * of the client of the surface that has it now enter it, or wait to until an input method
 * connects. No text input enters the lock screen. */
static void follow_focus(struct sh_server *server) {
    struct sh_input_methods *methods = &server->input_methods;
    struct wlr_surface *focus =
        server->locked ? NULL : server->seat->keyboard_state.focused_surface;
    struct sh_text_input *text_input;
    wl_list_for_each(text_input, &methods->text_inputs, link) {
        struct wlr_surface *entered = text_input->input->focused_surface;
        if (methods->active == text_input && (!entered || entered != focus))
            deactivate(server);
        if (text_input->pending && text_input->pending != focus)
            set_pending(text_input, NULL);
        if (entered && entered != focus)
            wlr_text_input_v3_send_leave(text_input->input);
        if (!focus || text_input->input->focused_surface == focus || text_input->pending ||
            wl_resource_get_client(text_input->input->resource) !=
                wl_resource_get_client(focus->resource))
            continue;
        if (methods->input_method)
            wlr_text_input_v3_send_enter(text_input->input, focus);
        else
            set_pending(text_input, focus);
    }
}

static void keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server =
        wl_container_of(listener, server, input_methods.keyboard_focus_change);
    follow_focus(server);
}

static void text_input_enable(struct wl_listener *listener, void *data) {
    struct sh_text_input *text_input = wl_container_of(listener, text_input, enable);
    activate(text_input);
}

static void text_input_commit(struct wl_listener *listener, void *data) {
    struct sh_text_input *text_input = wl_container_of(listener, text_input, commit);
    struct sh_input_methods *methods = &text_input->server->input_methods;
    if (!methods->input_method || !text_input->input->current_enabled)
        return;
    // One left enabled as its surface lost the keyboard is served again as it commits there.
    if (methods->active != text_input) {
        activate(text_input);
        return;
    }
    send_state(methods->input_method, text_input->input);
    place_popups(text_input->server);
}

static void text_input_disable(struct wl_listener *listener, void *data) {
    struct sh_text_input *text_input = wl_container_of(listener, text_input, disable);
    if (text_input->server->input_methods.active == text_input)
        deactivate(text_input->server);
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
    struct sh_text_input *text_input = wl_container_of(listener, text_input, destroy);
    if (text_input->server->input_methods.active == text_input)
        deactivate(text_input->server);
    set_pending(text_input, NULL);
    wl_list_remove(&text_input->enable.link);
    wl_list_remove(&text_input->commit.link);
    wl_list_remove(&text_input->disable.link);
    wl_list_remove(&text_input->destroy.link);
    wl_list_remove(&text_input->link);
    free(text_input);
}

static void new_text_input(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.new_text_input);
    struct wlr_text_input_v3 *input = data;
    if (input->seat != server->seat)
        return;
    struct sh_text_input *text_input = calloc(1, sizeof(*text_input));
    if (!text_input)
        return;
    text_input->server = server;
    text_input->input = input;
    text_input->pending_destroy.notify = pending_destroy;
    add_listener(&input->events.enable, &text_input->enable, text_input_enable);
    add_listener(&input->events.commit, &text_input->commit, text_input_commit);
    add_listener(&input->events.disable, &text_input->disable, text_input_disable);
    add_listener(&input->events.destroy, &text_input->destroy, text_input_destroy);
    wl_list_insert(&server->input_methods.text_inputs, &text_input->link);
    follow_focus(server); // it may belong to the client with the keyboard
}

/* What the input method commits goes to the text input it is active for: the preedit, the text
 * to insert, what to delete around the cursor. */
static void input_method_commit(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.commit);
    struct sh_input_methods *methods = &server->input_methods;
    struct sh_text_input *active = methods->active;
    if (!active || !active->input->focused_surface)
        return;
    struct wlr_input_method_v2_state *state = &methods->input_method->current;
    if (state->preedit.text)
        wlr_text_input_v3_send_preedit_string(active->input, state->preedit.text,
                                              state->preedit.cursor_begin,
                                              state->preedit.cursor_end);
    if (state->commit_text)
        wlr_text_input_v3_send_commit_string(active->input, state->commit_text);
    if (state->delete.before_length || state->delete.after_length)
        wlr_text_input_v3_send_delete_surrounding_text(active->input, state->delete.before_length,
                                                       state->delete.after_length);
    wlr_text_input_v3_send_done(active->input);
}

static void popup_map(struct wl_listener *listener, void *data) {
    struct sh_input_popup *popup = wl_container_of(listener, popup, map);
    place_popup(popup);
}

static void popup_unmap(struct wl_listener *listener, void *data) {
    struct sh_input_popup *popup = wl_container_of(listener, popup, unmap);
    wlr_scene_node_set_enabled(&popup->tree->node, false);
    popup->placed = false;
}

static void popup_commit(struct wl_listener *listener, void *data) {
    struct sh_input_popup *popup = wl_container_of(listener, popup, commit);
    if (popup->popup->surface->mapped)
        place_popup(popup); // its size may have changed
}

static void popup_destroy(struct wl_listener *listener, void *data) {
    struct sh_input_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->link);
    wlr_scene_node_destroy(&popup->tree->node);
    free(popup);
}

static void new_popup(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.new_popup);
    struct wlr_input_popup_surface_v2 *wlr_popup = data;
    struct sh_input_popup *popup = calloc(1, sizeof(*popup));
    if (!popup)
        return;
    popup->server = server;
    popup->popup = wlr_popup;
    popup->tree = wlr_scene_tree_create(server->input_methods.popup_tree);
    if (!popup->tree || !wlr_scene_subsurface_tree_create(popup->tree, wlr_popup->surface)) {
        if (popup->tree)
            wlr_scene_node_destroy(&popup->tree->node);
        free(popup);
        return;
    }
    wlr_scene_node_set_enabled(&popup->tree->node, false);
    popup->told = (struct wlr_box){-1, -1, -1, -1};
    add_listener(&wlr_popup->surface->events.map, &popup->map, popup_map);
    add_listener(&wlr_popup->surface->events.unmap, &popup->unmap, popup_unmap);
    add_listener(&wlr_popup->surface->events.commit, &popup->commit, popup_commit);
    add_listener(&wlr_popup->events.destroy, &popup->destroy, popup_destroy);
    wl_list_insert(&server->input_methods.popups, &popup->link);
    place_popup(popup);
}

/* A keyboard the input method's grab hears the keys of: the seat's, unless that is the input
 * method's own virtual keyboard, else a real one. */
static struct wlr_keyboard *grab_keyboard_for(struct sh_server *server) {
    struct wlr_keyboard *seat_keyboard = wlr_seat_get_keyboard(server->seat);
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (keyboard->wlr_keyboard == seat_keyboard && !keyboard->is_virtual)
            return seat_keyboard;
    }
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (!keyboard->is_virtual)
            return keyboard->wlr_keyboard;
    }
    return seat_keyboard;
}

static void grab_destroy(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.grab_destroy);
    wl_list_remove(&server->input_methods.grab_destroy.link);
    // The application has the modifiers as they are now, which went to the grab meanwhile.
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard && server->running)
        wlr_seat_keyboard_notify_modifiers(server->seat, &keyboard->modifiers);
}

static void grab_keyboard(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.grab_keyboard);
    struct wlr_input_method_keyboard_grab_v2 *grab = data;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, grab_keyboard_for(server));
    add_listener(&grab->events.destroy, &server->input_methods.grab_destroy, grab_destroy);
}

static void input_method_destroy(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.destroy);
    struct sh_input_methods *methods = &server->input_methods;
    wl_list_remove(&methods->commit.link);
    wl_list_remove(&methods->new_popup.link);
    wl_list_remove(&methods->grab_keyboard.link);
    wl_list_remove(&methods->destroy.link);
    methods->input_method = NULL;
    methods->active = NULL;
    // The text inputs leave, to enter again should another input method connect while the
    // surface still has the keyboard.
    struct sh_text_input *text_input;
    wl_list_for_each(text_input, &methods->text_inputs, link) {
        struct wlr_surface *entered = text_input->input->focused_surface;
        if (!entered)
            continue;
        wlr_text_input_v3_send_leave(text_input->input);
        set_pending(text_input, entered);
    }
    wlr_log(WLR_INFO, "The input method went away");
}

/* One input method at a time: another is told it is unavailable. */
static void new_input_method(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, input_methods.new_input_method);
    struct sh_input_methods *methods = &server->input_methods;
    struct wlr_input_method_v2 *input_method = data;
    if (methods->input_method || input_method->seat != server->seat) {
        wlr_log(WLR_INFO, "Refused an input method: one is connected already");
        wlr_input_method_v2_send_unavailable(input_method);
        return;
    }
    methods->input_method = input_method;
    add_listener(&input_method->events.commit, &methods->commit, input_method_commit);
    add_listener(&input_method->events.new_popup_surface, &methods->new_popup, new_popup);
    add_listener(&input_method->events.grab_keyboard, &methods->grab_keyboard, grab_keyboard);
    add_listener(&input_method->events.destroy, &methods->destroy, input_method_destroy);
    wlr_log(WLR_INFO, "An input method connected");
    // The text inputs that waited for one enter the surface with the keyboard.
    struct sh_text_input *text_input;
    wl_list_for_each(text_input, &methods->text_inputs, link) {
        struct wlr_surface *surface = text_input->pending;
        if (!surface)
            continue;
        set_pending(text_input, NULL);
        if (!text_input->input->focused_surface)
            wlr_text_input_v3_send_enter(text_input->input, surface);
    }
}

/* The input method's keyboard grab, when the keys of `keyboard` go to it: never while the
 * session is locked, nor from its own virtual keyboard, on which it types what it lets
 * through. */
static struct wlr_input_method_keyboard_grab_v2 *grab_for(struct sh_keyboard *keyboard) {
    struct sh_server *server = keyboard->server;
    struct wlr_input_method_v2 *input_method = server->input_methods.input_method;
    struct wlr_input_method_keyboard_grab_v2 *grab =
        input_method ? input_method->keyboard_grab : NULL;
    if (!grab || server->locked)
        return NULL;
    struct wlr_virtual_keyboard_v1 *virtual =
        wlr_input_device_get_virtual_keyboard(&keyboard->wlr_keyboard->base);
    if (virtual &&
        wl_resource_get_client(virtual->resource) == wl_resource_get_client(grab->resource))
        return NULL;
    return grab;
}

/* A key no binding took: the input method's grab gets it, true, or the application, false. A
 * key that went down to the grab comes up there too, or to the application if the grab has
 * gone, so that neither is left holding a key. */
bool input_method_key(struct sh_keyboard *keyboard, const struct wlr_keyboard_key_event *event) {
    struct wlr_input_method_keyboard_grab_v2 *grab = grab_for(keyboard);
    bool pressed = event->state == WL_KEYBOARD_KEY_STATE_PRESSED;
    bool known = event->keycode <= KEY_MAX;
    if (!pressed && known && !keyboard->to_input_method[event->keycode])
        return false; // it went down to the application
    if (known)
        keyboard->to_input_method[event->keycode] = pressed && grab;
    if (!grab)
        return false;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->wlr_keyboard);
    wlr_input_method_keyboard_grab_v2_send_key(grab, event->time_msec, event->keycode,
                                               event->state);
    return true;
}

/* The modifiers changed: the input method's grab hears it, true, or the application, false. */
bool input_method_modifiers(struct sh_keyboard *keyboard) {
    struct wlr_input_method_keyboard_grab_v2 *grab = grab_for(keyboard);
    if (!grab)
        return false;
    wlr_input_method_keyboard_grab_v2_set_keyboard(grab, keyboard->wlr_keyboard);
    wlr_input_method_keyboard_grab_v2_send_modifiers(grab, &keyboard->wlr_keyboard->modifiers);
    return true;
}

/* For `get input_method`: "input_method" with whether one is connected, whether it is active and
 * whether it grabs the keyboard (1 or 0 each); a line per text input, "text_input", whether it is
 * enabled and whether the input method serves it, with the surface it is on as `surface`
 * describes it; and a line per popup, "popup", whether it shows, and its place and size in the
 * layout. */
void describe_input_method(struct sh_server *server, int fd,
                           void (*surface)(struct sh_server *, int, const char *,
                                           struct wlr_surface *)) {
    struct sh_input_methods *methods = &server->input_methods;
    struct wlr_input_method_v2 *input_method = methods->input_method;
    char line[128];
    snprintf(line, sizeof(line), "input_method\t%d\t%d\t%d\n", input_method != NULL,
             methods->active != NULL, input_method && input_method->keyboard_grab);
    control_reply(fd, line);
    struct sh_text_input *text_input;
    wl_list_for_each_reverse(text_input, &methods->text_inputs, link) {
        snprintf(line, sizeof(line), "text_input\t%d\t%d", text_input->input->current_enabled,
                 methods->active == text_input);
        surface(server, fd, line, text_input->input->focused_surface);
    }
    struct sh_input_popup *popup;
    wl_list_for_each_reverse(popup, &methods->popups, link) {
        struct wlr_surface *popup_surface = popup->popup->surface;
        snprintf(line, sizeof(line), "popup\t%d\t%d\t%d\t%d\t%d\n",
                 popup->placed && popup->tree->node.enabled, popup->x, popup->y,
                 popup_surface->current.width, popup_surface->current.height);
        control_reply(fd, line);
    }
}

void input_method_init(struct sh_server *server) {
    struct sh_input_methods *methods = &server->input_methods;
    wl_list_init(&methods->text_inputs);
    wl_list_init(&methods->popups);
    // Above the windows, the panels and X11 menus, below the overview and the overlays; and over
    // the overlays for theirs.
    methods->popup_tree = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_node_place_above(&methods->popup_tree->node, &server->unmanaged->node);
    methods->overlay_popup_tree = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_node_place_above(&methods->overlay_popup_tree->node,
                               &server->layer_trees[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]->node);
    struct wlr_text_input_manager_v3 *text_inputs =
        wlr_text_input_manager_v3_create(server->wl_display);
    struct wlr_input_method_manager_v2 *input_methods =
        wlr_input_method_manager_v2_create(server->wl_display);
    add_listener(&text_inputs->events.new_text_input, &methods->new_text_input, new_text_input);
    add_listener(&input_methods->events.new_input_method, &methods->new_input_method,
                 new_input_method);
    add_listener(&server->seat->keyboard_state.events.focus_change, &methods->keyboard_focus_change,
                 keyboard_focus_change);
}

void input_method_finish(struct sh_server *server) {
    wl_list_remove(&server->input_methods.new_text_input.link);
    wl_list_remove(&server->input_methods.new_input_method.link);
    wl_list_remove(&server->input_methods.keyboard_focus_change.link);
}
