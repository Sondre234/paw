// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw tour` without a session (src/cli/tour_plan.cpp): its table of steps read and checked, which
 * steps a session lets run, their timing at a speed, the keys their captions show, the profile it
 * shows off, the windows it opens, and its options. */
#include "check.h"
#include "paw/tour.hpp"

#include <stdexcept>

namespace {
// The error parse_steps throws for `table`, or "" when it takes it.
std::string error_of(const std::string &table) {
    try {
        paw::tour::parse_steps(table);
    } catch (const std::runtime_error &error) {
        return error.what();
    }
    return "";
}

paw::tour::Options options(std::vector<const char *> arguments) {
    arguments.insert(arguments.begin(), {"paw", "tour"});
    return paw::tour::parse_options(static_cast<int>(arguments.size()),
                                    const_cast<char **>(arguments.data()));
}

bool refused(std::vector<const char *> arguments) {
    try {
        options(std::move(arguments));
    } catch (const std::runtime_error &) {
        return true;
    }
    return false;
}
} // namespace

int main() {
    using namespace paw::tour;

    // The default table reads, and keeps to its promises: a sentence each, a minute or so.
    auto steps = parse_steps(steps_table);
    CHECK(steps.size() >= 12, "%zu steps", steps.size());
    double total = 0;
    for (const auto &step : steps) {
        CHECK(!step.title.empty() && step.body.ends_with('.'), "%s", step.title.c_str());
        CHECK(step.body.size() <= 140, "%s: %zu characters", step.title.c_str(), step.body.size());
        total += step_seconds(step);
    }
    CHECK(total >= 60 && total <= 90, "%.1f seconds", total);
    CHECK(steps.front().title == "Welcome to paw");
    CHECK(steps.back().requests == std::vector<std::string>{"close windows"},
          "the last step closes the windows");

    // A line's fields: keys and requests split, "-" for none, and what it needs.
    auto parsed = parse_steps("# a comment\n\n"
                              " Tiling | One key tiles it. | toggle_tiling layout_next | tiling on;"
                              " wait 1.5 ;layout_master | 2\n"
                              "Quiet | Nothing. | - | - | 0 | needs xray shake\n");
    CHECK(parsed.size() == 2, "%zu", parsed.size());
    CHECK(parsed[0].title == "Tiling" && parsed[0].body == "One key tiles it.");
    CHECK((parsed[0].keys == std::vector<std::string>{"toggle_tiling", "layout_next"}));
    CHECK((parsed[0].requests ==
           std::vector<std::string>{"tiling on", "wait 1.5", "layout_master"}));
    CHECK(parsed[0].hold == 2 && parsed[0].needs.empty());
    NEAR(step_seconds(parsed[0]), 3.5, 1e-9);
    CHECK(parsed[1].keys.empty() && parsed[1].requests.empty());
    CHECK((parsed[1].needs == std::vector<std::string>{"xray", "shake"}));

    // Mistakes name their line.
    CHECK(error_of("\nA | B | - | - |\n").find("line 2") != std::string::npos,
          "%s", error_of("\nA | B | - | - |\n").c_str());
    CHECK(!error_of("A | B | - | -\n").empty(), "four fields");
    CHECK(!error_of(" | B | - | - | 1\n").empty(), "no title");
    CHECK(!error_of("A | B | - | wait | 1\n").empty(), "a wait without seconds");
    CHECK(!error_of("A | B | - | wait 61 | 1\n").empty(), "a wait too long");
    CHECK(!error_of("A | B | - | open 5 | 1\n").empty(), "too many windows");
    CHECK(!error_of("A | B | - | tiling maybe | 1\n").empty());
    CHECK(!error_of("A | B | - | focus mine | 1\n").empty());
    CHECK(!error_of("A | B | - | workspace {nowhere} | 1\n").empty(), "an unknown placeholder");
    CHECK(!error_of("A | B | - | close all | 1\n").empty());
    CHECK(error_of("A | B | - | close | 1\n").empty(), "close alone is the action");
    CHECK(!error_of("A | B | - | - | soon\n").empty(), "the hold is seconds");
    CHECK(!error_of("A | B | - | - | 1 | xray\n").empty(), "a sixth field starts with needs");
    CHECK(!error_of("A | B | - | a;;b | 1\n").empty(), "an empty request");

    // Which steps a session lets run.
    Offer offer{{"toggle_tiling", "xray"}, true, true, true};
    CHECK(skip_reason(parsed[0], offer).empty());
    CHECK(skip_reason(parsed[1], offer) == "the compositor has no shake",
          "%s", skip_reason(parsed[1], offer).c_str());
    offer.actions.insert("shake");
    CHECK(skip_reason(parsed[1], offer).empty());
    auto spare = parse_steps("W | Away and back. | - | workspace {spare}; workspace_back | 1\n")[0];
    auto profile = parse_steps("P | Look. | - | profile {other_profile}; profile {profile} | 1\n")[0];
    auto scratchpad = parse_steps("S | Hide. | - | move_to_scratchpad; scratchpad_show | 1\n")[0];
    CHECK(skip_reason(spare, offer).empty() && skip_reason(profile, offer).empty() &&
          skip_reason(scratchpad, offer).empty());
    offer = Offer{};
    CHECK(!skip_reason(spare, offer).empty() && !skip_reason(profile, offer).empty() &&
          !skip_reason(scratchpad, offer).empty());
    // In the default tour, each of those skips only its own step.
    size_t runnable = 0;
    Offer bare;
    for (const auto &step : steps) {
        Offer known = bare;
        for (const auto &need : step.needs)
            known.actions.insert(need);
        runnable += skip_reason(step, known).empty();
    }
    CHECK(runnable == steps.size() - 4, "%zu of %zu", runnable, steps.size());

    // Timing at a speed.
    CHECK(scaled_ms(1.5, 1) == 1500);
    CHECK(scaled_ms(1.5, 20) == 75);
    CHECK(scaled_ms(3, 0.5) == 6000);
    CHECK(scaled_ms(0, 3) == 0 && scaled_ms(-1, 1) == 0);
    CHECK(scaled_ms(0.001, 100) == 0);

    // Keys as captions show them.
    CHECK(key_label("Super + Tab") == "Super + Tab");
    CHECK(key_label("Super + Shift + minus") == "Super + Shift + -");
    CHECK(key_label("Super + grave") == "Super + `");
    CHECK(key_label("Super + bracketright") == "Super + ]");
    CHECK(key_label("Super + space") == "Super + Space");
    CHECK(key_label("Super + Return") == "Super + Enter");
    CHECK(key_label("XF86AudioMute") == "AudioMute");
    CHECK(key_label("Alt + Tab") == "Alt + Tab");
    std::map<std::string, std::string> bindings{{"workspace_next", "Super + Ctrl + Right"},
                                                {"workspace_back", "Super + Tab"},
                                                {"layout_scroll", "-"},
                                                {"move_to_scratchpad", "Super + Shift + minus"}};
    CHECK(caption_keys({"workspace_next", "workspace_back"}, bindings) ==
          "Super + Ctrl + Right / Super + Tab");
    CHECK(caption_keys({"layout_scroll"}, bindings) == "paw msg layout_scroll",
          "an action bound to no key is named as paw msg runs it");
    CHECK(caption_keys({"layout_scroll", "move_to_scratchpad"}, bindings) ==
          "Super + Shift + -", "only the bound ones when any is");
    CHECK(caption_keys({"night_light_toggle"}, {}) == "paw msg night_light_toggle");
    CHECK(caption_keys({}, bindings).empty());

    // The profile shown off: a twin, else the next one.
    std::vector<std::string> profiles{"default", "light", "macos-dark", "macos-light", "sumi",
                                      "washi"};
    CHECK(other_profile(profiles, "macos-light") == "macos-dark");
    CHECK(other_profile(profiles, "macos-dark") == "macos-light");
    CHECK(other_profile(profiles, "washi") == "sumi");
    CHECK(other_profile(profiles, "sumi") == "washi");
    CHECK(other_profile(profiles, "light") == "macos-dark", "no plain dark: the next one");
    CHECK(other_profile(profiles, "default") == "light");
    CHECK(other_profile({"a", "b"}, "b") == "a", "wrapping");
    CHECK(other_profile({"only"}, "only").empty());
    CHECK(other_profile(profiles, "").empty(), "none in use to come back to");
    CHECK(other_profile(profiles, "gone").empty());

    // The windows it opens.
    CHECK(shell_quote("paw tour 1") == "'paw tour 1'");
    CHECK(shell_quote("it's") == "'it'\\''s'");
    CHECK((terminal_command({"kitty"}, "T", "S") ==
           std::vector<std::string>{"kitty", "--title", "T", "sh", "-c", "S"}));
    CHECK((terminal_command({"/usr/bin/foot", "-o", "x=1"}, "T", "S") ==
           std::vector<std::string>{"/usr/bin/foot", "-o", "x=1", "--title", "T", "sh", "-c",
                                    "S"}));
    CHECK((terminal_command({"alacritty"}, "T", "S") ==
           std::vector<std::string>{"alacritty", "--title", "T", "-e", "sh", "-c", "S"}));
    CHECK((terminal_command({"xterm"}, "T", "S") ==
           std::vector<std::string>{"xterm", "-T", "T", "-e", "sh", "-c", "S"}));
    CHECK((terminal_command({"ghostty"}, "T", "S") ==
           std::vector<std::string>{"ghostty", "--title=T", "-e", "sh", "-c", "S"}));
    CHECK((terminal_command({"wezterm"}, "T", "S") ==
           std::vector<std::string>{"wezterm", "start", "--always-new-process", "--", "sh", "-c",
                                    "S"}));
    CHECK((terminal_command({"gnome-terminal"}, "T", "S") ==
           std::vector<std::string>{"gnome-terminal", "--wait", "--title", "T", "--", "sh", "-c",
                                    "S"}));
    CHECK((terminal_command({"konsole"}, "T", "S") ==
           std::vector<std::string>{"konsole", "-e", "sh", "-c", "S"}));
    CHECK(terminal_command({}, "T", "S").empty());
    CHECK(client_command("probe --title {title} --again {title}", "paw tour 2") ==
          "probe --title 'paw tour 2' --again 'paw tour 2'");
    CHECK(client_command("foot", "x") == "foot");

    // What requests act on.
    CHECK(acts_on_window("snap_left") && acts_on_window("toggle_sticky") &&
          acts_on_window("group_toggle") && acts_on_window("move_to_scratchpad") &&
          acts_on_window("close") && acts_on_window("resize_right 40"));
    CHECK(!acts_on_window("scratchpad_show") && !acts_on_window("workspace 2") &&
          !acts_on_window("profile washi") && !acts_on_window("wait 1"));
    CHECK(acts_on_output("workspace 3") && acts_on_output("workspace_back") &&
          acts_on_output("toggle_tiling") && acts_on_output("layout_scroll") &&
          acts_on_output("toggle_overview") && acts_on_output("switcher"));
    CHECK(!acts_on_output("snap_left") && !acts_on_output("overview filter x") &&
          !acts_on_output("profile x") && !acts_on_output("night_light_toggle"));
    std::map<std::string, std::string> values{{"spare", "3"}, {"profile", "washi"}};
    CHECK(expand("workspace {spare}", values) == "workspace 3");
    CHECK(expand("profile {profile}; {spare}{spare}", values) == "profile washi; 33");
    CHECK(expand("toggle_tiling", values) == "toggle_tiling");
    bool unknown = false;
    try {
        expand("profile {other_profile}", values);
    } catch (const std::runtime_error &) {
        unknown = true;
    }
    CHECK(unknown, "a placeholder without a value throws");

    // Options.
    auto plain = options({});
    CHECK(!plain.list && !plain.step && plain.from == 1 && plain.speed == 1 && plain.restore &&
          plain.client.empty());
    auto all = options({"--list", "--step", "--from", "4", "--speed", "2.5", "--no-restore",
                        "--client", "probe {title}"});
    CHECK(all.list && all.step && all.from == 4 && all.speed == 2.5 && !all.restore &&
          all.client == "probe {title}");
    CHECK(refused({"--from", "0"}) && refused({"--from", "x"}) && refused({"--from"}));
    CHECK(refused({"--speed", "0"}) && refused({"--speed", "1000"}) &&
          refused({"--speed", "fast"}));
    CHECK(refused({"--client", " "}) && refused({"--fast"}) && refused({"extra"}));
    return failures != 0;
}
