/* SPDX-License-Identifier: GPL-3.0-or-later */
/* HDR (outputs.monitors' hdr) and colour management. A monitor asking for HDR is driven in
 * BT.2020 with the PQ curve where its EDID offers both, the renderer can convert colours (in
 * wlroots 0.20 only Vulkan can) and the backend's test passes; the scene then converts every
 * window's colours for it. color-management-v1, through which applications such as mpv and games
 * describe their colours, is offered once a monitor asks for HDR, and only with such a renderer:
 * another would show what clients describe unconverted. */
#include "server.h"
#include <wlr/types/wlr_color_management_v1.h>

/* Offers color-management-v1 once a monitor asks for HDR (hdr_asked, display_settings.c) and
 * the renderer can convert colours, and from then on; at startup, after each reload and as the
 * display settings window applies its settings. */
void color_management_update(struct sh_server *server) {
    if (server->color_manager || !hdr_asked(server))
        return;
    struct wlr_renderer *renderer = server->renderer;
    if (!renderer->features.input_color_transform || !renderer->features.output_color_transform) {
        if (!server->color_manager_refused)
            wlr_log(WLR_INFO, "color-management-v1 is not offered: the renderer cannot convert "
                              "colours (in wlroots 0.20 only Vulkan can; WLR_RENDERER=vulkan)");
        server->color_manager_refused = true;
        return;
    }
    static const enum wp_color_manager_v1_render_intent intents[] = {
        WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL};
    size_t functions_length = 0, primaries_length = 0;
    enum wp_color_manager_v1_transfer_function *functions =
        wlr_color_manager_v1_transfer_function_list_from_renderer(renderer, &functions_length);
    enum wp_color_manager_v1_primaries *primaries =
        wlr_color_manager_v1_primaries_list_from_renderer(renderer, &primaries_length);
    server->color_manager = wlr_color_manager_v1_create(
        server->wl_display, 1,
        &(struct wlr_color_manager_v1_options){
            .features = {.parametric = true, .set_mastering_display_primaries = true},
            .render_intents = intents,
            .render_intents_len = sizeof(intents) / sizeof(*intents),
            .transfer_functions = functions,
            .transfer_functions_len = functions_length,
            .primaries = primaries,
            .primaries_len = primaries_length,
        });
    free(functions);
    free(primaries);
    if (!server->color_manager)
        return;
    wlr_scene_set_color_manager_v1(server->scene, server->color_manager);
    wlr_log(WLR_INFO, "Offering color-management-v1");
}

/* Under --headless, whose outputs have no EDID, the outputs PAW_TEST_HDR names (separated by
 * commas) say they take BT.2020 with PQ, as an HDR monitor's EDID does; for tests. */
static void fake_hdr_monitor(struct sh_output *output) {
    const char *named = getenv("PAW_TEST_HDR");
    if (!named || !headless_backend(output->server))
        return;
    const char *name = output->wlr_output->name;
    size_t length = strlen(name);
    for (const char *at = strstr(named, name); at; at = strstr(at + 1, name)) {
        if ((at == named || at[-1] == ',') && (at[length] == ',' || at[length] == '\0')) {
            output->wlr_output->supported_primaries |= WLR_COLOR_NAMED_PRIMARIES_BT2020;
            output->wlr_output->supported_transfer_functions |=
                WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ;
        }
    }
}

/* Why `output` cannot be driven in HDR whatever its settings, or NULL when it can be tried: the
 * monitor's EDID offers no BT.2020 with PQ (or its connector lacks the properties), or the
 * renderer cannot convert colours. */
const char *hdr_unavailable(struct sh_output *output) {
    struct wlr_output *wlr_output = output->wlr_output;
    fake_hdr_monitor(output);
    if (!(wlr_output->supported_primaries & WLR_COLOR_NAMED_PRIMARIES_BT2020) ||
        !(wlr_output->supported_transfer_functions & WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ))
        return "the monitor does not offer BT.2020 with PQ";
    if (!output->server->renderer->features.output_color_transform)
        return "the renderer cannot convert colours (in wlroots 0.20 only Vulkan can)";
    return NULL;
}

/* Why `output` cannot be driven in HDR now, or NULL when it can be tried: as hdr_unavailable, or
 * it mirrors another monitor, whose picture it shows as it is. */
static const char *hdr_refused(struct sh_output *output, bool mirrors) {
    const char *unavailable = hdr_unavailable(output);
    if (unavailable)
        return unavailable;
    if (mirrors)
        return "it mirrors another monitor";
    return NULL;
}

/* Puts HDR into `state` for an output whose settings ask for it and may have it, or takes it
 * away from one that has it and should not; true when HDR is asked of the state. The caller tests
 * the state and drops it with output_drop_hdr where the test fails. */
bool output_want_hdr(struct sh_output *output, const struct sh_monitor *monitor, bool mirrors,
                     struct wlr_output_state *state) {
    struct wlr_output *wlr_output = output->wlr_output;
    const char *refused = monitor && monitor->hdr ? hdr_refused(output, mirrors) : "";
    if (refused && *refused && refused != output->hdr_refused)
        wlr_log(WLR_ERROR, "%s stays SDR: %s", wlr_output->name, refused);
    output->hdr_refused = refused;
    if (refused) {
        if (wlr_output->image_description) {
            wlr_output_state_set_image_description(state, NULL);
            state->allow_reconfiguration = true; // a monitor's colour space changes on a modeset
        }
        return false;
    }
    const struct wlr_output_image_description *current = wlr_output->image_description;
    if (current && current->primaries == WLR_COLOR_NAMED_PRIMARIES_BT2020 &&
        current->transfer_function == WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ)
        return true;
    wlr_output_state_set_image_description(
        state, &(struct wlr_output_image_description){
                   .primaries = WLR_COLOR_NAMED_PRIMARIES_BT2020,
                   .transfer_function = WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ,
               });
    state->allow_reconfiguration = true;
    return true;
}

/* The test refused HDR: the output stays as it was, SDR. */
void output_drop_hdr(struct sh_output *output, struct wlr_output_state *state) {
    static const char *const refused = "its test refused HDR";
    if (output->hdr_refused != refused)
        wlr_log(WLR_ERROR, "%s stays SDR: %s", output->wlr_output->name, refused);
    output->hdr_refused = refused;
    if (output->wlr_output->image_description)
        wlr_output_state_set_image_description(state, NULL);
    else
        state->committed &= ~WLR_OUTPUT_STATE_IMAGE_DESCRIPTION;
}

/* Whether `output` is driven in HDR now. */
bool output_is_hdr(const struct sh_output *output) {
    const struct wlr_output_image_description *current = output->wlr_output->image_description;
    return current && current->transfer_function == WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ;
}
