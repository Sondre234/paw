/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Derived from wlroots TinyWL 0.20.2; see vendor/tinywl/LICENSE. */
/* Server setup and teardown, configuration reload, signal handlers, and small helpers the
 * other files share. */
#include "server.h"

_Static_assert((unsigned)SH_ALT == (unsigned)WLR_MODIFIER_ALT &&
                   (unsigned)SH_SHIFT == (unsigned)WLR_MODIFIER_SHIFT &&
                   (unsigned)SH_CTRL == (unsigned)WLR_MODIFIER_CTRL &&
                   (unsigned)SH_LOGO == (unsigned)WLR_MODIFIER_LOGO,
               "C++ configuration and wlroots modifier bits must agree");
_Static_assert((unsigned)SH_EDGE_TOP == (unsigned)WLR_EDGE_TOP &&
                   (unsigned)SH_EDGE_BOTTOM == (unsigned)WLR_EDGE_BOTTOM &&
                   (unsigned)SH_EDGE_LEFT == (unsigned)WLR_EDGE_LEFT &&
                   (unsigned)SH_EDGE_RIGHT == (unsigned)WLR_EDGE_RIGHT,
               "tiling and wlroots edge bits must agree");

uint64_t now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

int64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Starts a program with an ordinary signal mask (the compositor blocks signals for its event
 * loop); the child is reaped with the others. 0, or why it could not start. */
int spawn_program(char *const argv[]) {
    posix_spawnattr_t attributes;
    int error = posix_spawnattr_init(&attributes);
    if (error)
        return error;
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t pid;
    extern char **environ;
    error = posix_spawnp(&pid, argv[0], NULL, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    return error;
}

/* A protocol object's destroy request, for one with nothing more to do. */
void destroy_resource(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

void add_listener(struct wl_signal *signal, struct wl_listener *listener,
                  wl_notify_func_t notify) {
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

const struct sh_settings *server_settings(struct sh_server *server) {
    return server->callbacks->settings(server->callbacks->userdata);
}

static void configure_animations(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_animator_config config = {.enabled = settings->animations,
                                        .speed = settings->animation_speed,
                                        .late_ms = settings->animation_late_ms};
    memcpy(config.styles, settings->animation_styles, sizeof(config.styles));
    sh_animator_configure(server->animator, &config);
}

void reload_config(struct sh_server *server) {
    if (!server->callbacks->reload(server->callbacks->userdata))
        return;
    set_binding_mode(server, 0);
    struct sh_output *overridden;
    for_each_connected_output(overridden, server) overridden->has_override = false;
    ++server->config_generation;
    display_settings_load(server); // laid over outputs.monitors, so read with them
    configure_animations(server);
    night_light_update(server);
    color_management_update(server);
    if (!server_settings(server)->overview)
        overview_dismiss(server);
    update_keymap(server);
    struct sh_pointer *pointer;
    wl_list_for_each(pointer, &server->pointers, link) configure_pointer(server, pointer->device);
    configure_outputs(server);
    arrange_outputs(server);
    reconfigure_tiling(server);
    return_home_windows(server);
    struct sh_toplevel *toplevel;
    // With features.sticky off, sticky windows stay on their output's current workspace.
    if (!server_settings(server)->sticky) {
        wl_list_for_each(toplevel, &server->toplevels, link) set_sticky(toplevel, false, true);
    }
    rehome_tiles(server);
    int count = server_settings(server)->workspaces;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->workspace >= count)
            set_toplevel_workspace(toplevel, count - 1);
    }
    for (size_t i = 0; i < sizeof(server->output_workspaces) / sizeof(server->output_workspaces[0]);
         ++i) {
        if (server->output_workspaces[i].current >= count)
            server->output_workspaces[i].current = count - 1;
        if (server->output_workspaces[i].previous >= count ||
            server->output_workspaces[i].previous == server->output_workspaces[i].current)
            server->output_workspaces[i].previous = -1;
    }
    if (!server_settings(server)->scratchpad)
        empty_scratchpad(server);
    if (!server_settings(server)->groups)
        dissolve_groups(server);
    restack_windows(server); // layout.floating_above_tiles may have changed
    struct sh_toplevel *next;
    wl_list_for_each_safe(toplevel, next, &server->toplevels, link) follow_dynamic_rules(toplevel);
    show_workspaces(server);
    refocus_if_hidden(server);
    // Gaps, borders, and opacity may have changed.
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
    power_reload(server);
    idle_reload(server);
    shortcuts_reload(server);
}

static int terminate_signal(int signal_number, void *data) {
    struct sh_server *server = data;
    wl_display_terminate(server->wl_display);
    return 0;
}
static int reload_signal(int signal_number, void *data) {
    reload_config(data);
    return 0;
}
#define CONFIG_SETTLE_MS 150
static int config_settled(void *data) {
    reload_config(data);
    return 0;
}
static int config_watch_ready(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    if (server->callbacks->config_changed(server->callbacks->userdata) && server->config_timer)
        wl_event_source_timer_update(server->config_timer, CONFIG_SETTLE_MS);
    return 0;
}
static int reap_children(int signal_number, void *data) {
    struct sh_server *server = data;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        if (server->callbacks->child_exited)
            server->callbacks->child_exited(server->callbacks->userdata, pid);
    }
    return 0;
}

/* Says why no Wayland socket could be made; libwayland only logs each name it tried. */
static void report_socket_failure(void) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un address;
    if (!runtime || !*runtime)
        wlr_log(WLR_ERROR, "Cannot create a Wayland socket: XDG_RUNTIME_DIR is not set");
    else if (strlen(runtime) + strlen("/wayland-0") >= sizeof(address.sun_path))
        wlr_log(WLR_ERROR,
                "Cannot create a Wayland socket: XDG_RUNTIME_DIR (%s) is too long for a socket "
                "path, which holds %zu bytes",
                runtime, sizeof(address.sun_path) - 1);
    else if (access(runtime, W_OK | X_OK) != 0)
        wlr_log_errno(WLR_ERROR, "Cannot create a Wayland socket in XDG_RUNTIME_DIR (%s)", runtime);
    else
        wlr_log(WLR_ERROR, "Cannot create a Wayland socket in XDG_RUNTIME_DIR (%s)", runtime);
}

int sh_run(const struct sh_callbacks *callbacks, enum sh_backend_mode mode) {
    wlr_log_init(WLR_INFO, NULL);
    if (mode == SH_BACKEND_SESSION &&
        !(WLR_HAS_SESSION && WLR_HAS_DRM_BACKEND && WLR_HAS_LIBINPUT_BACKEND)) {
        wlr_log(WLR_ERROR, "wlroots needs session, DRM, and libinput support for --session");
        return 1;
    }
    const char *backends = mode == SH_BACKEND_SESSION    ? "drm,libinput"
                           : mode == SH_BACKEND_HEADLESS ? "headless"
                                                         : "wayland";
    if (setenv("WLR_BACKENDS", backends, 1) < 0)
        return 1;
    if (mode == SH_BACKEND_HEADLESS)
        setenv("WLR_HEADLESS_OUTPUTS", "1", 0); // tests may ask for more

    struct sh_server server = {
        .callbacks = callbacks, .config_generation = 1, .power.sleep_delay = -1};
    wl_list_init(&server.subscribers);
    server.tiling = sh_tiling_create();
    if (!server.tiling)
        return 1;

    server.wl_display = wl_display_create();
    if (!server.wl_display)
        return 1;
    struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
    struct wl_event_source *sigint =
        wl_event_loop_add_signal(loop, SIGINT, terminate_signal, &server);
    struct wl_event_source *sigterm =
        wl_event_loop_add_signal(loop, SIGTERM, terminate_signal, &server);
    struct wl_event_source *sighup = wl_event_loop_add_signal(loop, SIGHUP, reload_signal, &server);
    struct wl_event_source *sigchld =
        wl_event_loop_add_signal(loop, SIGCHLD, reap_children, &server);
    server.animator = sh_animator_create(loop);
    if (!server.animator)
        return 1;
    configure_animations(&server);
    configure_layouts(&server);
    server.night_kelvin = SH_KELVIN_NEUTRAL;
    server.night_announced = -1;
    sh_fade_init(&server.zoom_fade, 1);
    server.zoom_target = 1;
    server.night_clock = -1;
    if (getenv("PAW_NIGHT_LIGHT_TIME")) {
        // A fixed clock, so tests can run at any hour.
        double minutes = 0;
        if (sh_parse_clock(getenv("PAW_NIGHT_LIGHT_TIME"), &minutes))
            server.night_clock = minutes;
    }
    server.night_timer = wl_event_loop_add_timer(loop, night_light_tick, &server);
    server.corner_timer = wl_event_loop_add_timer(loop, hot_corner_tick, &server);
    server.urgent_timer = wl_event_loop_add_timer(loop, urgent_tick, &server);
    int config_fd = callbacks->config_watch(callbacks->userdata);
    if (config_fd >= 0) {
        server.config_timer = wl_event_loop_add_timer(loop, config_settled, &server);
        server.config_watch = wl_event_loop_add_fd(loop, config_fd, WL_EVENT_READABLE,
                                                   config_watch_ready, &server);
    }
    sh_corner_dwell_init(&server.corner_dwell);

    server.backend = wlr_backend_autocreate(loop,
#if WLR_HAS_SESSION
                                            &server.session
#else
                                            NULL
#endif
    );
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }
#if WLR_HAS_SESSION
    server.sleep_inhibitor = -1;
    if (server.session) {
        add_listener(&server.session->events.active, &server.session_active, session_active);
        session_active(&server.session_active, NULL);
    }
#endif

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }

    wlr_renderer_init_wl_shm(server.renderer, server.wl_display);
    // GPU clients (browsers, Electron, games) share buffers by dmabuf; the scene sends them
    // scanout feedback, and explicit sync keeps NVIDIA from showing unfinished frames.
    struct wlr_linux_dmabuf_v1 *linux_dmabuf = NULL;
    if (wlr_renderer_get_texture_formats(server.renderer, WLR_BUFFER_CAP_DMABUF))
        linux_dmabuf =
            wlr_linux_dmabuf_v1_create_with_renderer(server.wl_display, 4, server.renderer);
    int drm_fd = wlr_renderer_get_drm_fd(server.renderer);
    if (drm_fd >= 0 && server.renderer->features.timeline && server.backend->features.timeline)
        wlr_linux_drm_syncobj_manager_v1_create(server.wl_display, 1, drm_fd);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    struct wlr_compositor *compositor =
        wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);
    wlr_primary_selection_v1_device_manager_create(server.wl_display);
    wlr_data_control_manager_v1_create(server.wl_display);
    wlr_ext_data_control_manager_v1_create(server.wl_display, 1);
    wlr_viewporter_create(server.wl_display);
    wlr_fractional_scale_manager_v1_create(server.wl_display, 1);
    wlr_single_pixel_buffer_manager_v1_create(server.wl_display);
    wlr_presentation_create(server.wl_display, server.backend, 2);
    wlr_xdg_wm_dialog_v1_create(server.wl_display, 1);
    // Portals parent their file choosers and share dialogs to the requesting window.
    struct wlr_xdg_foreign_registry *foreign_registry =
        wlr_xdg_foreign_registry_create(server.wl_display);
    wlr_xdg_foreign_v1_create(server.wl_display, foreign_registry);
    wlr_xdg_foreign_v2_create(server.wl_display, foreign_registry);

    server.output_layout = wlr_output_layout_create(server.wl_display);
    wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

    wl_list_init(&server.outputs);
    wl_list_init(&server.disabled_outputs);
    add_listener(&server.backend->events.new_output, &server.new_output, server_new_output);

    server.scene = wlr_scene_create();
    if (linux_dmabuf)
        wlr_scene_set_linux_dmabuf_v1(server.scene, linux_dmabuf);
    wlr_scene_set_gamma_control_manager_v1(server.scene,
                                           wlr_gamma_control_manager_v1_create(server.wl_display));
    // Stacking order, bottom to top.
    struct wlr_scene_tree **stack[] = {
        &server.backgrounds, &server.layer_trees[0], &server.layer_trees[1],
        &server.windows,     &server.floating_windows, &server.above_windows,
        &server.fullscreen,     &server.peek_layer,     &server.layer_trees[2],
        &server.fullscreen_cover,
        &server.unmanaged,   &server.guide_layer,    &server.overview_layer, &server.layer_trees[3], &server.drag_icons,
        &server.lock_tree,
    };
    for (size_t i = 0; i < sizeof(stack) / sizeof(stack[0]); ++i)
        *stack[i] = wlr_scene_tree_create(&server.scene->tree);
    server.lock_blanks = wlr_scene_tree_create(server.lock_tree);
    wlr_scene_node_set_enabled(&server.lock_tree->node, false);
    struct wlr_session_lock_manager_v1 *lock_manager =
        wlr_session_lock_manager_v1_create(server.wl_display);
    add_listener(&lock_manager->events.new_lock, &server.new_lock, server_new_lock);
    wl_list_init(&server.layers);
    struct wlr_layer_shell_v1 *layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    add_listener(&layer_shell->events.new_surface, &server.new_layer_surface,
                 server_new_layer_surface);
    server.foreign_manager = wlr_foreign_toplevel_manager_v1_create(server.wl_display);
    window_control_init(&server);
    // Screen capture for screenshots and portal screen sharing (xdg-desktop-portal-wlr).
    wlr_screencopy_manager_v1_create(server.wl_display);
    wlr_export_dmabuf_manager_v1_create(server.wl_display);
    wlr_ext_image_copy_capture_manager_v1_create(server.wl_display, 1);
    wlr_ext_output_image_capture_source_manager_v1_create(server.wl_display, 1);
    server.toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(server.wl_display, 1);
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *toplevel_capture =
        wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(server.wl_display, 1);
    add_listener(&toplevel_capture->events.new_request, &server.new_capture_request,
                 server_new_capture_request);
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    wl_list_init(&server.toplevels);
    struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    add_listener(&xdg_shell->events.new_toplevel, &server.new_xdg_toplevel,
                 server_new_xdg_toplevel);
    add_listener(&xdg_shell->events.new_popup, &server.new_xdg_popup, server_new_xdg_popup);
    struct wlr_xdg_decoration_manager_v1 *decorations =
        wlr_xdg_decoration_manager_v1_create(server.wl_display);
    add_listener(&decorations->events.new_toplevel_decoration, &server.new_decoration,
                 server_new_decoration);
    // Windows' tearing hints, which windows.allow_tearing follows (tearing.c).
    server.tearing_control = wlr_tearing_control_manager_v1_create(server.wl_display, 1);
    // The monitors' settings the display settings window kept, before any monitor appears.
    display_settings_load(&server);
    // Applications' colours, once a monitor asks for HDR (hdr.c).
    color_management_update(&server);
    // Windows' own icons, for the taskbar (window_icon.c). Clients are told the sizes taskbars
    // and switchers commonly draw, up to the 256 pixels the shell is sent at most.
    struct wlr_xdg_toplevel_icon_manager_v1 *icons =
        wlr_xdg_toplevel_icon_manager_v1_create(server.wl_display, 1);
    int icon_sizes[] = {16, 24, 32, 48, 64, 128, 256};
    wlr_xdg_toplevel_icon_manager_v1_set_sizes(icons, icon_sizes,
                                               sizeof(icon_sizes) / sizeof(*icon_sizes));
    add_listener(&icons->events.set_icon, &server.set_xdg_icon, server_set_xdg_icon);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

    /* Without a theme, wlroots draws the bare X11 arrow. Adwaita ships with GTK, so it is
     * nearly always installed; $XCURSOR_THEME and $XCURSOR_SIZE still win. Exporting both lets
     * applications draw the same cursor over their windows. */
    const char *cursor_theme = getenv("XCURSOR_THEME");
    if (!cursor_theme || !*cursor_theme)
        cursor_theme = "Adwaita";
    const char *size_env = getenv("XCURSOR_SIZE");
    int cursor_size = size_env ? atoi(size_env) : 0;
    if (cursor_size <= 0)
        cursor_size = 24;
    char size_text[16];
    snprintf(size_text, sizeof(size_text), "%d", cursor_size);
    setenv("XCURSOR_THEME", cursor_theme, true);
    setenv("XCURSOR_SIZE", size_text, true);
    server.cursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    add_listener(&server.cursor->events.motion, &server.cursor_motion, server_cursor_motion);
    add_listener(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute,
                 server_cursor_motion_absolute);
    add_listener(&server.cursor->events.button, &server.cursor_button, server_cursor_button);
    add_listener(&server.cursor->events.axis, &server.cursor_axis, server_cursor_axis);
    add_listener(&server.cursor->events.frame, &server.cursor_frame, server_cursor_frame);
    gestures_init(&server);
    touch_init(&server);

    wl_list_init(&server.keyboards);
    wl_list_init(&server.headless_devices);
    wl_list_init(&server.switches);
    wl_list_init(&server.pointers);
    add_listener(&server.backend->events.new_input, &server.new_input, server_new_input);
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboards =
        wlr_virtual_keyboard_manager_v1_create(server.wl_display);
    add_listener(&virtual_keyboards->events.new_virtual_keyboard, &server.new_virtual_keyboard,
                 server_new_virtual_keyboard);
    struct wlr_virtual_pointer_manager_v1 *virtual_pointers =
        wlr_virtual_pointer_manager_v1_create(server.wl_display);
    add_listener(&virtual_pointers->events.new_virtual_pointer, &server.new_virtual_pointer,
                 server_new_virtual_pointer);
    server.seat = wlr_seat_create(server.wl_display, "seat0");
    // Always offered, so clients bind a keyboard even before one (maybe virtual) appears.
    wlr_seat_set_capabilities(server.seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    update_keymap(&server); // for the keyboards the backend finds as it starts
    add_listener(&server.seat->events.request_set_cursor, &server.request_cursor,
                 seat_request_cursor);
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr =
        wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
    add_listener(&cursor_shape_mgr->events.request_set_shape, &server.request_set_shape,
                 cursor_request_set_shape);
    add_listener(&server.seat->pointer_state.events.focus_change, &server.pointer_focus_change,
                 seat_pointer_focus_change);
    add_listener(&server.seat->events.request_set_selection, &server.request_set_selection,
                 seat_request_set_selection);
    add_listener(&server.seat->events.request_set_primary_selection,
                 &server.request_set_primary_selection, seat_request_set_primary_selection);
    add_listener(&server.seat->events.request_start_drag, &server.request_start_drag,
                 seat_request_start_drag);
    add_listener(&server.seat->events.start_drag, &server.start_drag, seat_start_drag);
    add_listener(&server.seat->keyboard_state.events.focus_change, &server.keyboard_focus_change,
                 seat_keyboard_focus_change);
    tablet_init(&server);
    shortcuts_inhibit_init(&server);
    input_method_init(&server);
    struct wlr_xdg_activation_v1 *activation = wlr_xdg_activation_v1_create(server.wl_display);
    add_listener(&activation->events.request_activate, &server.request_activate, request_activate);
    server.relative_pointer = wlr_relative_pointer_manager_v1_create(server.wl_display);
    server.constraints = wlr_pointer_constraints_v1_create(server.wl_display);
    add_listener(&server.constraints->events.new_constraint, &server.new_constraint,
                 server_new_constraint);
    server.idle_notifier = wlr_idle_notifier_v1_create(server.wl_display);
    // wlr-randr, kanshi, and graphical display settings tools.
    server.output_manager = wlr_output_manager_v1_create(server.wl_display);
    add_listener(&server.output_manager->events.apply, &server.output_apply, output_config_apply);
    add_listener(&server.output_manager->events.test, &server.output_test, output_config_test);
    // wlopm and idle daemons turn monitors off and on, keeping them in the layout.
    struct wlr_output_power_manager_v1 *output_power =
        wlr_output_power_manager_v1_create(server.wl_display);
    add_listener(&output_power->events.set_mode, &server.output_power_set_mode,
                 output_power_set_mode);
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit =
        wlr_idle_inhibit_v1_create(server.wl_display);
    add_listener(&idle_inhibit->events.new_inhibitor, &server.new_inhibitor, server_new_inhibitor);

    // From here every listener is in place, so a failure undoes the whole start, as quitting
    // does: wlroots aborts when a backend goes while something still listens to it.
    int status = 1;
    const char *socket = wl_display_add_socket_auto(server.wl_display);
    if (!socket) {
        report_socket_failure();
        goto finish;
    }
    if (!wlr_backend_start(server.backend)) {
        wlr_log(WLR_ERROR, "Cannot start the backend (%s)", backends);
        goto finish;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    open_control_socket(&server, socket);
    setenv("XDG_CURRENT_DESKTOP", "paw", true);
    setenv("XDG_SESSION_TYPE", "wayland", true);
    // Firefox, Electron (Discord, VS Code), and Java would otherwise need to be told to use
    // Wayland or to cope without a reparenting window manager. The user's own values win.
    setenv("MOZ_ENABLE_WAYLAND", "1", false);
    setenv("ELECTRON_OZONE_PLATFORM_HINT", "auto", false);
    setenv("_JAVA_AWT_WM_NONREPARENTING", "1", false);
    unsetenv("DISPLAY");
#if WLR_HAS_XWAYLAND
    // Xwayland starts when the first X11 client connects and exits once idle.
    if (server_settings(&server)->xwayland) {
        server.xwayland = wlr_xwayland_create(server.wl_display, compositor, true);
        if (server.xwayland) {
            add_listener(&server.xwayland->events.ready, &server.xwayland_ready, xwayland_ready);
            add_listener(&server.xwayland->events.new_surface, &server.new_xwayland_surface,
                         server_new_xwayland_surface);
            setenv("DISPLAY", server.xwayland->display_name, true);
            wlr_log(WLR_INFO, "XWayland listening on DISPLAY=%s", server.xwayland->display_name);
        } else {
            wlr_log(WLR_ERROR, "Cannot create XWayland; X11 applications are unavailable");
        }
    }
#else
    (void)compositor;
#endif
    night_light_update(&server);
    server.power.system_bus = mode != SH_BACKEND_HEADLESS;
    power_init(&server);
    idle_init(&server);
    server.running = true;
    server.login_session = mode == SH_BACKEND_SESSION ||
                           (getenv("PAW_LOGIN_SESSION") && *getenv("PAW_LOGIN_SESSION"));
    callbacks->startup(callbacks->userdata);
    session_restore_last(&server);

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);
    server.running = false;
    status = 0;

#if WLR_HAS_XWAYLAND
#if PAW_XWM_WAKER
    close_xwm_waker(&server);
#endif
    if (server.xwayland) {
        wl_list_remove(&server.xwayland_ready.link);
        wl_list_remove(&server.new_xwayland_surface.link);
        wlr_xwayland_destroy(server.xwayland);
    }
#endif
    power_finish(&server);
    idle_finish(&server);
    close_control_socket(&server);

finish:
    wl_event_source_remove(sigint);
    wl_event_source_remove(sigterm);
    wl_event_source_remove(sighup);
    wl_event_source_remove(sigchld);
    destroy_headless_inputs(&server);
    wl_display_destroy_clients(server.wl_display);

    wl_list_remove(&server.new_xdg_toplevel.link);
    wl_list_remove(&server.new_xdg_popup.link);
    wl_list_remove(&server.new_decoration.link);
    wl_list_remove(&server.set_xdg_icon.link);
    wl_list_remove(&server.new_layer_surface.link);

    wl_list_remove(&server.cursor_motion.link);
    wl_list_remove(&server.cursor_motion_absolute.link);
    wl_list_remove(&server.cursor_button.link);
    wl_list_remove(&server.cursor_axis.link);
    wl_list_remove(&server.cursor_frame.link);
    gestures_finish(&server);
    touch_finish(&server);
    tablet_finish(&server);
    shortcuts_inhibit_finish(&server);
    input_method_finish(&server);

    wl_list_remove(&server.new_input.link);
    wl_list_remove(&server.new_virtual_keyboard.link);
    wl_list_remove(&server.new_virtual_pointer.link);
    wl_list_remove(&server.request_cursor.link);
    wl_list_remove(&server.request_set_shape.link);
    wl_list_remove(&server.pointer_focus_change.link);
    wl_list_remove(&server.request_set_selection.link);
    wl_list_remove(&server.request_set_primary_selection.link);
    wl_list_remove(&server.request_start_drag.link);
    wl_list_remove(&server.start_drag.link);
    wl_list_remove(&server.keyboard_focus_change.link);
    wl_list_remove(&server.request_activate.link);
    wl_list_remove(&server.new_constraint.link);
    wl_list_remove(&server.new_capture_request.link);

    wl_list_remove(&server.new_output.link);
    wl_list_remove(&server.new_lock.link);
    wl_list_remove(&server.new_inhibitor.link);
    wl_list_remove(&server.output_apply.link);
    wl_list_remove(&server.output_test.link);
    wl_list_remove(&server.output_power_set_mode.link);
#if WLR_HAS_SESSION
    if (server.session)
        wl_list_remove(&server.session_active.link);
    if (server.sleep_inhibitor >= 0)
        close(server.sleep_inhibitor);
#endif

    if (server.night_timer)
        wl_event_source_remove(server.night_timer);
    if (server.corner_timer)
        wl_event_source_remove(server.corner_timer);
    if (server.overview.timer)
        wl_event_source_remove(server.overview.timer);
    if (server.urgent_timer)
        wl_event_source_remove(server.urgent_timer);
    display_mode_finish(&server);
    display_settings_finish(&server);
    if (server.config_watch)
        wl_event_source_remove(server.config_watch);
    if (server.config_timer)
        wl_event_source_remove(server.config_timer);
    if (server.night_transform)
        wlr_color_transform_unref(server.night_transform);
    wlr_backend_destroy(server.backend);
    sh_animator_destroy(server.animator);
    wlr_scene_node_destroy(&server.scene->tree.node);
    for (size_t i = 0; i < sizeof(server.deco_buffers) / sizeof(*server.deco_buffers); ++i)
        wlr_buffer_drop(server.deco_buffers[i]);
    for (size_t i = 0; i < sizeof(server.shadow_images) / sizeof(*server.shadow_images); ++i)
        wlr_buffer_drop(server.shadow_images[i].buffer);
    wlr_buffer_drop(server.black);
    wlr_xcursor_manager_destroy(server.cursor_mgr);
    wlr_cursor_destroy(server.cursor);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wl_display_destroy(server.wl_display);
    sh_tiling_destroy(server.tiling);
    xkb_keymap_unref(server.keymap);
    return status;
}
