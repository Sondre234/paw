/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Touchpad gestures: the swipes the compositor takes for itself (`gestures`: workspaces that
 * follow the fingers, the overview opening and closing with them, requests), and every other
 * swipe, pinch and hold of the cursor's devices passed on to the surface the pointer is on
 * through pointer-gestures-unstable-v1, for browsers' pinch-zoom and GTK's gestures. */
#include "server.h"

static const char *mode_names[] = {"none", "waiting", "passed", "workspace", "overview", "action"};

static const struct sh_swipe_binding *find_swipe(struct sh_server *server, int fingers,
                                                 enum sh_swipe_direction direction) {
    const struct sh_gesture_settings *gestures = &server_settings(server)->gestures;
    for (int i = 0; i < gestures->swipe_count; ++i) {
        if (gestures->swipes[i].fingers == fingers && gestures->swipes[i].direction == (int)direction)
            return &gestures->swipes[i];
    }
    return NULL;
}

/* Whether the compositor may take a swipe of `fingers`: one of its swipes has as many. The
 * lock screen gets every gesture. */
static bool takes_fingers(struct sh_server *server, int fingers) {
    const struct sh_gesture_settings *gestures = &server_settings(server)->gestures;
    if (!gestures->enabled || server->locked)
        return false;
    for (int i = 0; i < gestures->swipe_count; ++i) {
        if (gestures->swipes[i].fingers == fingers)
            return true;
    }
    return false;
}

/* Hands a swipe that waited to the window under the pointer, from its beginning. */
static void pass_on(struct sh_server *server, uint32_t time) {
    struct sh_gesture *gesture = &server->gesture;
    gesture->mode = SH_SWIPE_PASSED;
    wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures, server->seat,
                                             gesture->began, gesture->swipe.fingers);
    if (gesture->dx || gesture->dy)
        wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures, server->seat, time,
                                                  gesture->dx, gesture->dy);
}

static int workspace_step(enum sh_action action) {
    return action == SH_WORKSPACE_NEXT ? 1 : action == SH_WORKSPACE_PREV ? -1 : 0;
}

/* Shows where the fingers are: the workspaces or the overview part of the way. */
static void follow(struct sh_server *server) {
    struct sh_gesture *gesture = &server->gesture;
    double progress = sh_swipe_progress(&gesture->swipe);
    if (gesture->mode == SH_SWIPE_WORKSPACE) {
        // Back past where they began, the fingers head for the opposite swipe's workspace.
        int step = progress >= 0           ? workspace_step(gesture->bound.action)
                   : gesture->has_opposite ? workspace_step(gesture->opposite.action)
                                           : 0;
        workspace_swipe_hold(server, &gesture->workspace, gesture->workspace.from + step,
                             fabs(progress));
    } else if (gesture->mode == SH_SWIPE_OVERVIEW) {
        overview_hold(server, gesture->opening ? progress : 1 - progress);
    }
}

/* The swipe has its direction: the compositor takes it if one of its swipes is bound there. */
static void decide(struct sh_server *server, uint32_t time) {
    struct sh_gesture *gesture = &server->gesture;
    enum sh_swipe_direction direction = gesture->swipe.direction;
    const struct sh_swipe_binding *bound = find_swipe(server, gesture->swipe.fingers, direction);
    if (!bound) {
        pass_on(server, time);
        return;
    }
    gesture->bound = *bound;
    const struct sh_swipe_binding *opposite =
        find_swipe(server, gesture->swipe.fingers, sh_swipe_opposite(direction));
    gesture->has_opposite = opposite != NULL;
    if (opposite)
        gesture->opposite = *opposite;
    gesture->mode = SH_SWIPE_ACTION;
    if (workspace_step(bound->action) && !server->overview.open) {
        // The monitor under the pointer, as the fingers are there.
        struct wlr_output *output = pointer_output(server);
        if (!output)
            output = focused_output(server);
        if (output) {
            workspace_swipe_begin(server, &gesture->workspace, output);
            gesture->mode = SH_SWIPE_WORKSPACE;
        }
    } else if (bound->action == SH_OVERVIEW_TOGGLE || bound->action == SH_OVERVIEW_CANCEL) {
        gesture->opening = bound->action == SH_OVERVIEW_TOGGLE && !server->overview.open;
        if (gesture->opening)
            overview_open(server);
        gesture->mode = SH_SWIPE_OVERVIEW;
    }
    wlr_log(WLR_INFO, "Swipe of %d fingers %s: %s", gesture->swipe.fingers,
            sh_swipe_direction_name(direction), bound->request);
    follow(server);
}

/* What a swipe bound to an action does as the fingers lift far enough. */
static void run_swipe(struct sh_server *server, const struct sh_swipe_binding *bound) {
    struct sh_overview *overview = &server->overview;
    int step = workspace_step(bound->action);
    if (step && overview->open) {
        // The overview shows the next or previous workspace, as its strip does.
        overview_view(server, overview->viewed + step);
        return;
    }
    int argument = 0;
    char error[256] = "";
    enum sh_action action = server->callbacks->command(server->callbacks->userdata,
                                                       bound->request, &argument, error,
                                                       sizeof(error));
    if (action != SH_NONE)
        run_action(server, action, argument);
    else if (error[0])
        wlr_log(WLR_ERROR, "Swipe: %s: %s", bound->request, error);
}

/* The fingers lift (or libinput gives the swipe up): the step goes on or back from there. */
static void finish_swipe(struct sh_server *server, uint32_t time, bool cancelled) {
    struct sh_gesture *gesture = &server->gesture;
    cancelled = cancelled || server->locked; // the screen locked under the fingers
    double progress = sh_swipe_progress(&gesture->swipe);
    double speed = sh_swipe_speed(&gesture->swipe, time);
    switch (gesture->mode) {
    case SH_SWIPE_WORKSPACE: {
        // Toward whichever workspace the fingers are on the side of.
        bool forward = progress >= 0;
        workspace_swipe_end(server, &gesture->workspace,
                            !cancelled && sh_swipe_finishes(forward ? progress : -progress,
                                                            forward ? speed : -speed));
        break;
    }
    case SH_SWIPE_OVERVIEW: {
        bool done = !cancelled && sh_swipe_finishes(progress, speed);
        overview_release(server, gesture->opening ? done : !done);
        break;
    }
    case SH_SWIPE_ACTION:
        if (!cancelled && sh_swipe_finishes(progress, speed))
            run_swipe(server, &gesture->bound);
        break;
    default:
        break;
    }
    gesture->mode = SH_SWIPE_IDLE;
}

static void swipe_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;
    struct sh_gesture *gesture = &server->gesture;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (gesture->mode != SH_SWIPE_IDLE && gesture->mode != SH_SWIPE_PASSED)
        finish_swipe(server, event->time_msec, true); // another device's, given up
    const struct sh_gesture_settings *settings = &server_settings(server)->gestures;
    sh_swipe_begin(&gesture->swipe, (int)event->fingers, settings->distance, settings->invert,
                   event->time_msec);
    gesture->began = event->time_msec;
    gesture->dx = gesture->dy = 0;
    if (takes_fingers(server, (int)event->fingers)) {
        gesture->mode = SH_SWIPE_WAITING;
        return;
    }
    gesture->mode = SH_SWIPE_PASSED;
    wlr_pointer_gestures_v1_send_swipe_begin(server->pointer_gestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void swipe_update(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;
    struct sh_gesture *gesture = &server->gesture;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    switch (gesture->mode) {
    case SH_SWIPE_IDLE:
        break;
    case SH_SWIPE_PASSED:
        wlr_pointer_gestures_v1_send_swipe_update(server->pointer_gestures, server->seat,
                                                  event->time_msec, event->dx, event->dy);
        break;
    case SH_SWIPE_WAITING:
        gesture->dx += event->dx;
        gesture->dy += event->dy;
        if (sh_swipe_update(&gesture->swipe, event->dx, event->dy, event->time_msec))
            decide(server, event->time_msec);
        break;
    default:
        if (server->locked) { // what the compositor took ends with the screen locking
            finish_swipe(server, event->time_msec, true);
            break;
        }
        sh_swipe_update(&gesture->swipe, event->dx, event->dy, event->time_msec);
        follow(server);
        break;
    }
}

static void swipe_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;
    struct sh_gesture *gesture = &server->gesture;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (gesture->mode == SH_SWIPE_WAITING)
        pass_on(server, event->time_msec); // too short to have a way: the window's, whole
    if (gesture->mode == SH_SWIPE_PASSED) {
        wlr_pointer_gestures_v1_send_swipe_end(server->pointer_gestures, server->seat,
                                               event->time_msec, event->cancelled);
        gesture->mode = SH_SWIPE_IDLE;
        return;
    }
    finish_swipe(server, event->time_msec, event->cancelled);
}

static void pinch_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_begin(server->pointer_gestures, server->seat,
                                             event->time_msec, event->fingers);
}

static void pinch_update(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_update(server->pointer_gestures, server->seat,
                                              event->time_msec, event->dx, event->dy,
                                              event->scale, event->rotation);
}

static void pinch_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_pinch_end(server->pointer_gestures, server->seat,
                                           event->time_msec, event->cancelled);
}

/* Fingers resting on the touchpad: GTK and Firefox stop kinetic scrolling on a hold. */
static void hold_begin(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_hold_begin(server->pointer_gestures, server->seat,
                                            event->time_msec, event->fingers);
}

static void hold_end(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, hold_end);
    struct wlr_pointer_hold_end_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    wlr_pointer_gestures_v1_send_hold_end(server->pointer_gestures, server->seat,
                                          event->time_msec, event->cancelled);
}

/* `get gesture`: the swipe under way (what it is, its fingers, direction and progress in
 * thousandths of a step), and for workspaces following it, the output, the workspace it began
 * on and the one it heads for (from 1; 0 for none), how far the slide is held (thousandths),
 * and how many windows are held and coming in as copies. */
void describe_gesture(struct sh_server *server, int fd) {
    struct sh_gesture *gesture = &server->gesture;
    char line[256];
    snprintf(line, sizeof(line), "%s\t%d\t%s\t%ld\n", mode_names[gesture->mode],
             gesture->mode ? gesture->swipe.fingers : 0,
             sh_swipe_direction_name(gesture->mode ? gesture->swipe.direction : SH_SWIPE_NONE),
             gesture->mode ? lround(sh_swipe_progress(&gesture->swipe) * 1000) : 0);
    control_reply(fd, line);
    if (gesture->mode != SH_SWIPE_WORKSPACE)
        return;
    struct sh_workspace_swipe *swipe = &gesture->workspace;
    int held = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) held += toplevel->anim.held;
    int count = server_settings(server)->workspaces;
    snprintf(line, sizeof(line), "workspace\t%s\t%d\t%d\t%ld\t%d\t%d\n", swipe->output,
             swipe->from + 1,
             swipe->target >= 0 && swipe->target < count && swipe->target != swipe->from
                 ? swipe->target + 1
                 : 0,
             lround(swipe->shown * 1000), held, swipe->copy_count);
    control_reply(fd, line);
}

/* Offers pointer-gestures-unstable-v1 and listens to the cursor's gestures. wlroots sends each
 * one to the client of the surface with pointer focus, if it bound the gestures of that seat's
 * pointer. */
void gestures_init(struct sh_server *server) {
    server->pointer_gestures = wlr_pointer_gestures_v1_create(server->wl_display);
    struct wlr_cursor *cursor = server->cursor;
    add_listener(&cursor->events.swipe_begin, &server->swipe_begin, swipe_begin);
    add_listener(&cursor->events.swipe_update, &server->swipe_update, swipe_update);
    add_listener(&cursor->events.swipe_end, &server->swipe_end, swipe_end);
    add_listener(&cursor->events.pinch_begin, &server->pinch_begin, pinch_begin);
    add_listener(&cursor->events.pinch_update, &server->pinch_update, pinch_update);
    add_listener(&cursor->events.pinch_end, &server->pinch_end, pinch_end);
    add_listener(&cursor->events.hold_begin, &server->hold_begin, hold_begin);
    add_listener(&cursor->events.hold_end, &server->hold_end, hold_end);
}

void gestures_finish(struct sh_server *server) {
    wl_list_remove(&server->swipe_begin.link);
    wl_list_remove(&server->swipe_update.link);
    wl_list_remove(&server->swipe_end.link);
    wl_list_remove(&server->pinch_begin.link);
    wl_list_remove(&server->pinch_update.link);
    wl_list_remove(&server->pinch_end.link);
    wl_list_remove(&server->hold_begin.link);
    wl_list_remove(&server->hold_end.link);
}
