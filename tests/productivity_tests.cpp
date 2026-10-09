// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of workspace names, window groups, and sessions.
#include "paw/config.hpp"
#include "paw/session.h"
#include "paw/tabs.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rejects(const std::string &source, const std::string &fragment = "") {
    try {
        (void)paw::parse_config(source);
    } catch (const std::exception &error) {
        if (!fragment.empty() && std::string(error.what()).find(fragment) == std::string::npos)
            throw std::runtime_error("wrong error for " + source + ": " + error.what());
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}

void workspace_names() {
    auto config = paw::parse_config(
        "return {layout={workspaces=5,workspace_names={'web','code','','chat much'}},"
        "bindings={{mods={'Super'},key='w',action='workspace',workspace='code'},"
        "{mods={'Super'},key='e',action='move_to_workspace',workspace='chat much'},"
        "{mods={'Super'},key='r',action='workspace',workspace=3}}}");
    require(config.workspace_names.size() == 4, "names not read");
    require(config.workspace_number("web") == 1 && config.workspace_number("code") == 2 &&
                config.workspace_number("chat much") == 4,
            "names not resolved");
    require(config.workspace_number("3") == 3 && config.workspace_number("5") == 5,
            "numbers not resolved");
    require(!config.workspace_number("6") && !config.workspace_number("nope") &&
                !config.workspace_number("") && !config.workspace_number("0"),
            "bad workspace resolved");
    require(config.workspace_label(1) == "1 web" && config.workspace_label(3) == "3" &&
                config.workspace_label(5) == "5" && config.workspace_label(9) == "9",
            "labels wrong");
    require(config.binding(SH_LOGO, XKB_KEY_w)->workspace == 2 &&
                config.binding(SH_LOGO, XKB_KEY_e)->workspace == 4 &&
                config.binding(SH_LOGO, XKB_KEY_r)->workspace == 3,
            "binding workspace names not resolved");
    require(paw::parse_config("return {}").workspace_names.empty(), "default has names");
    rejects("return {layout={workspaces=2,workspace_names={'a','b','c'}}}", "more names");
    rejects("return {layout={workspace_names={'a','a'}}}", "duplicate");
    rejects("return {layout={workspace_names={'12'}}}", "mistaken for a number");
    rejects("return {layout={workspace_names={' a'}}}", "space");
    rejects("return {layout={workspace_names={'a\\nb'}}}", "control");
    rejects("return {layout={workspace_names={string.rep('x',33)}}}", "longer than 32");
    rejects("return {layout={workspace_names={1}}}");
    rejects("return {layout={workspace_names='web'}}");
    rejects("return {bindings={{mods={'Super'},key='w',action='workspace',workspace='web'}}}",
            "no workspace named");
    rejects("return {layout={workspace_names={'a'}},"
            "bindings={{mods={'Super'},key='w',action='close',workspace='a'}}}");
}

void session_encoding() {
    char out[256];
    require(sh_session_encode("a b\tc%d\n", false, out, sizeof(out)) == 14 &&
                !strcmp(out, "a b%09c%25d%0A"),
            "field encoding");
    require(!strcmp((sh_session_encode("a b", true, out, sizeof(out)), out), "a%20b"),
            "argument encoding");
    require(sh_session_encode("abcdef", false, out, 4) == 6 && !strcmp(out, "abc"),
            "truncated encoding reports the full length");
    size_t length = sh_session_decode("a%20b%zz%4", 10, out, sizeof(out));
    require(length == 8 && !strcmp(out, "a b%zz%4"), "decoding leaves bad escapes alone");
    require(sh_session_valid_name("work") && sh_session_valid_name("a.b-c_1") &&
                !sh_session_valid_name("") && !sh_session_valid_name(".hidden") &&
                !sh_session_valid_name("a/b") && !sh_session_valid_name("..") &&
                !sh_session_valid_name("a b") && !sh_session_valid_name(std::string(64, 'x').c_str()),
            "session names");
    sh_session_window window{};
    const char cmdline[] = "kitty\0--title\0two words\0%\0";
    sh_session_set_command(&window, cmdline, sizeof(cmdline) - 1);
    require(!strcmp(window.command, "kitty --title two%20words %25"), "command encoding");
    std::unique_ptr<char *, decltype(&free)> argv(sh_session_argv(window.command), free);
    require(argv && !strcmp(argv.get()[0], "kitty") && !strcmp(argv.get()[1], "--title") &&
                !strcmp(argv.get()[2], "two words") && !strcmp(argv.get()[3], "%") &&
                !argv.get()[4],
            "command decoding");
    require(!sh_session_argv("") && !sh_session_argv(nullptr), "empty command");
    window.command[0] = '\0';
    std::string huge(2000, 'x');
    huge.push_back('\0');
    sh_session_set_command(&window, huge.c_str(), huge.size());
    require(!window.command[0], "an over-long command is dropped");
}

void session_file() {
    auto session = std::make_unique<sh_session>();
    *session = {};
    strcpy(session->outputs[0].name, "DP-1");
    session->outputs[0].workspace = 2;
    session->outputs[0].tiling = 1;
    strcpy(session->outputs[1].name, "HDMI-A-1");
    session->outputs[1].tiling = -1;
    session->output_count = 2;
    strcpy(session->layouts[0].output, "DP-1");
    session->layouts[0].workspace = 1;
    session->layouts[0].layout = 1;
    session->layouts[0].ratio = 0.6;
    session->layouts[0].master_count = 2;
    strcpy(session->layouts[1].output, "DP-1");
    session->layouts[1].workspace = 2;
    session->layouts[1].layout = 4;
    session->layouts[1].ratio = 0.55;
    session->layouts[1].master_count = 1;
    session->layouts[1].widths[0] = 0.5;
    session->layouts[1].widths[1] = 1.0 / 3;
    session->layouts[1].width_count = 2;
    session->layout_count = 2;
    auto &w = session->windows[0];
    strcpy(w.output, "DP-1");
    w.workspace = 1;
    w.flags = SH_SESSION_TILED | SH_SESSION_FOCUSED;
    w.x = -5, w.y = 7, w.width = 640, w.height = 480;
    strcpy(w.app_id, "org.example.App");
    strcpy(w.title, "tab\there 100% \xc3\xa5");
    strcpy(w.command, "app --flag two%20words");
    session->windows[1] = {};
    strcpy(session->windows[1].output, "HDMI-A-1");
    session->windows[1].scroll_column = 2;
    session->windows[1].scroll_row = 1;
    session->window_count = 2;

    char *buffer = nullptr;
    size_t size = 0;
    FILE *out = open_memstream(&buffer, &size);
    require(out && sh_session_write(session.get(), out), "session not written");
    fclose(out);
    std::unique_ptr<char, decltype(&free)> text(buffer, free);
    require(std::count(buffer, buffer + size, '\n') == 1 + 2 + 2 + 2, "one record per line");
    FILE *in = fmemopen(buffer, size, "r");
    auto back = std::make_unique<sh_session>();
    char error[128] = "";
    require(sh_session_read(back.get(), in, error, sizeof(error)), error);
    fclose(in);
    require(back->output_count == 2 && !strcmp(back->outputs[0].name, "DP-1") &&
                back->outputs[0].workspace == 2 && back->outputs[0].tiling == 1 &&
                back->outputs[1].tiling == -1 && back->layout_count == 2 &&
                back->layouts[0].layout == 1 && back->layouts[0].master_count == 2 &&
                back->layouts[0].ratio > 0.59 && back->layouts[0].ratio < 0.61 &&
                back->window_count == 2,
            "outputs and layouts did not round-trip");
    auto &r = back->windows[0];
    require(!strcmp(r.output, "DP-1") && r.workspace == 1 &&
                r.flags == (SH_SESSION_TILED | SH_SESSION_FOCUSED) && r.x == -5 && r.y == 7 &&
                r.width == 640 && r.height == 480 && !strcmp(r.app_id, "org.example.App") &&
                !strcmp(r.title, w.title) && !strcmp(r.command, w.command),
            "window did not round-trip");
    require(!back->windows[1].command[0] && !back->windows[1].app_id[0], "empty fields");
    require(back->layouts[0].width_count == 0 && back->layouts[1].width_count == 2 &&
                std::abs(back->layouts[1].widths[1] - 1.0 / 3) < 1e-4 &&
                back->windows[0].scroll_column == 0 && back->windows[1].scroll_column == 2 &&
                back->windows[1].scroll_row == 1,
            "scroll columns did not round-trip");

    auto bad = [](const char *body, const char *fragment) {
        FILE *file = fmemopen(const_cast<char *>(body), strlen(body), "r");
        auto session = std::make_unique<sh_session>();
        char error[128] = "";
        bool ok = sh_session_read(session.get(), file, error, sizeof(error));
        fclose(file);
        require(!ok && strstr(error, fragment), body);
    };
    bad("", "not a paw session");
    bad("something else\n", "not a paw session");
    bad("paw-session 1\nbogus\tx\n", "line 2");
    bad("paw-session 1\noutput\tDP-1\tx\t1\n", "line 2");
    bad("paw-session 1\noutput\tDP-1\t-1\t1\n", "line 2");
    bad("paw-session 1\nwindow\tDP-1\t0\t0\t0\t0\t1\n", "line 2");
    bad("paw-session 1\nlayout\tDP-1\t0\t1\tnan?\t1\n", "line 2");
    bad("paw-session 1\nlayout\tDP-1\t0\t4\t0.5\t1\t0.5,x\n", "line 2");
    bad("paw-session 1\nlayout\tDP-1\t0\t4\t0.5\t1\t2\n", "line 2");
    bad("paw-session 1\nwindow\tDP-1\t0\t0\t0\t0\t1\t1\ta\tb\t\t0\t1\n", "line 2");
}

void session_matching() {
    sh_session_window saved[4] = {};
    const char *apps[] = {"kitty", "kitty", "firefox", "gone"};
    const char *titles[] = {"one", "two", "Home", "x"};
    for (int i = 0; i < 4; ++i) {
        strcpy(saved[i].app_id, apps[i]);
        strcpy(saved[i].title, titles[i]);
    }
    // Live windows: two kitties (titles swapped from the saved order), firefox, and another.
    const char *live_apps[] = {"kitty", "other", "kitty", "firefox"};
    const char *live_titles[] = {"two", "two", "changed", "New tab"};
    int assignment[4];
    int pairs = sh_session_match(saved, 4, live_apps, live_titles, 4, assignment);
    require(pairs == 3, "three pairs");
    require(assignment[1] == 0, "the exact title wins over order");
    require(assignment[0] == 2, "then the app ID alone");
    require(assignment[2] == 3 && assignment[3] == -1, "firefox paired, missing one left");
    // Nothing is paired twice.
    sh_session_window same[2] = {};
    strcpy(same[0].app_id, "a");
    strcpy(same[1].app_id, "a");
    const char *one_app[] = {"a"}, *one_title[] = {""};
    int result[2];
    require(sh_session_match(same, 2, one_app, one_title, 1, result) == 1 &&
                result[0] == 0 && result[1] == -1,
            "a live window is paired once");
    require(sh_session_match(same, 0, one_app, one_title, 1, result) == 0, "nothing saved");
}

void session_paths() {
    char path[512];
    setenv("XDG_STATE_HOME", "/tmp/state", 1);
    require(sh_session_path("work", path, sizeof(path)) &&
                !strcmp(path, "/tmp/state/paw/sessions/work"),
            "session path");
    require(sh_session_path(nullptr, path, sizeof(path)) &&
                !strcmp(path, "/tmp/state/paw/sessions"),
            "session directory");
    require(!sh_session_path("../x", path, sizeof(path)), "path traversal refused");
    require(!sh_session_path("work", path, 8), "short buffer refused");
    unsetenv("XDG_STATE_HOME");
    setenv("HOME", "/home/x", 1);
    require(sh_session_path("w", path, sizeof(path)) &&
                !strcmp(path, "/home/x/.local/state/paw/sessions/w"),
            "home fallback");
}

void group_config() {
    auto defaults = paw::parse_config("return {}");
    require(defaults.settings.groups && defaults.settings.group_join_new, "groups off by default");
    auto config = paw::parse_config(
        "return {features={groups=false,group_join_new=false},bindings={"
        "{mods={'Super'},key='g',action='group_toggle'},"
        "{mods={'Super'},key='h',action='group_next'},"
        "{mods={'Super'},key='j',action='group_prev'},"
        "{mods={'Super'},key='k',action='ungroup'},"
        "{mods={'Super'},key='a',action='group_merge_left'},"
        "{mods={'Super'},key='b',action='group_merge_right'},"
        "{mods={'Super'},key='c',action='group_merge_up'},"
        "{mods={'Super'},key='d',action='group_merge_down'}}}");
    require(!config.settings.groups && !config.settings.group_join_new, "features not read");
    const std::pair<unsigned, sh_action> expected[] = {
        {XKB_KEY_g, SH_GROUP_TOGGLE},      {XKB_KEY_h, SH_GROUP_NEXT},
        {XKB_KEY_j, SH_GROUP_PREV},        {XKB_KEY_k, SH_UNGROUP},
        {XKB_KEY_a, SH_GROUP_MERGE_LEFT},  {XKB_KEY_b, SH_GROUP_MERGE_RIGHT},
        {XKB_KEY_c, SH_GROUP_MERGE_UP},    {XKB_KEY_d, SH_GROUP_MERGE_DOWN}};
    for (auto [key, action] : expected) {
        const auto *binding = config.binding(SH_LOGO, key);
        require(binding && binding->action == action, "group binding wrong");
    }
    // The merge actions are consecutive, in the order the compositor reads them.
    static_assert(SH_GROUP_MERGE_RIGHT == SH_GROUP_MERGE_LEFT + 1 &&
                      SH_GROUP_MERGE_UP == SH_GROUP_MERGE_LEFT + 2 &&
                      SH_GROUP_MERGE_DOWN == SH_GROUP_MERGE_LEFT + 3,
                  "merge actions not in order");
    rejects("return {features={groups='yes'}}", "features.groups");
    rejects("return {features={groupz=true}}", "groupz");
}

void tab_strip() {
    for (int width : {1, 7, 100, 801, 1919}) {
        for (int count = 1; count <= 9; ++count) {
            int previous_end = -SH_TABS_GAP;
            for (int i = 0; i < count; ++i) {
                int x, length;
                sh_tabs_span(width, count, i, &x, &length);
                require(x >= previous_end + SH_TABS_GAP, "tabs overlap");
                require(length >= 1, "empty tab");
                if (width >= 20 * count)
                    require(x + length <= width, "tab past the strip");
                previous_end = x + length;
            }
            if (width >= 20 * count) {
                int x, length;
                sh_tabs_span(width, count, count - 1, &x, &length);
                require(x + length == width, "strip not filled");
            }
        }
    }
    // Every column of the strip belongs to some tab, in order; outside it, none.
    int last = 0;
    for (int x = 0; x < 300; ++x) {
        int tab = sh_tabs_index_at(300, 4, x + 0.5);
        require(tab >= last && tab < 4, "tab order wrong");
        last = tab;
    }
    require(last == 3 && sh_tabs_index_at(300, 4, -1) == -1 && sh_tabs_index_at(300, 4, 300) == -1 &&
                sh_tabs_index_at(300, 0, 5) == -1 && sh_tabs_index_at(300, 1, 299) == 0,
            "tab lookup wrong");
    int x, length;
    sh_tabs_span(300, 3, 1, &x, &length);
    require(sh_tabs_index_at(300, 3, x + length / 2.0) == 1, "middle of a tab not found");

    // The lit tab is brighter than the others; the corners are transparent.
    for (int scale : {1, 2}) {
        int width = 90;
        std::vector<uint32_t> pixels(static_cast<size_t>(width) * scale * SH_TABS_HEIGHT * scale);
        sh_tabs_paint(pixels.data(), width, scale, 3, 1, -1);
        auto at = [&](int tab, int px, int py) {
            int start, span;
            sh_tabs_span(width, 3, tab, &start, &span);
            return pixels[static_cast<size_t>(py) * width * scale + (start + span / 2) * scale + px];
        };
        int mid = SH_TABS_HEIGHT * scale / 2;
        require((at(1, 0, mid) & 0xff) > (at(0, 0, mid) & 0xff) &&
                    (at(1, 0, mid) >> 24) == 255 && (at(0, 0, mid) >> 24) > 0,
                "active tab not lit");
        require(pixels[0] == 0, "corner of the strip not transparent");
        int gap_start, gap_length;
        sh_tabs_span(width, 3, 0, &gap_start, &gap_length);
        require(pixels[static_cast<size_t>(mid) * width * scale + (gap_start + gap_length) * scale] == 0,
                "gap between tabs painted");
    }
}
} // namespace

int main() {
    try {
        workspace_names();
        session_encoding();
        session_file();
        session_matching();
        session_paths();
        group_config();
        tab_strip();
    } catch (const std::exception &error) {
        std::cerr << "productivity test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "productivity tests passed\n";
    return 0;
}
