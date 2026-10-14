// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* `paw tour` (src/cli/tour.cpp), a narrated demo that drives the running session: its steps, in a
 * table of their own (src/cli/tour_steps.cpp), and what of it needs no session (src/cli/
 * tour_plan.cpp): reading the table, which steps can run, their timing, and what their captions
 * and windows say. tests/tour_tests.cpp tests these. */
#include <map>
#include <set>
#include <string>
#include <vector>

namespace paw::tour {

// A step of the tour: its caption, what it does through the control socket, and how long it
// stays after that.
struct Step {
    std::string title;
    std::string body; // one sentence
    // The actions whose keys the caption names, as the user has them bound.
    std::vector<std::string> keys;
    // Control requests in order, the tour's own among them (see tour_steps.cpp).
    std::vector<std::string> requests;
    double hold = 0; // seconds
    // Actions the compositor has to know for the step to run; without them it is skipped.
    std::vector<std::string> needs;
};

// The default tour's table (tour_steps.cpp).
extern const char *const steps_table;
// The steps of a table; throws std::runtime_error naming the line of a mistake.
std::vector<Step> parse_steps(const std::string &table);

// What the session offers the steps.
struct Offer {
    std::set<std::string> actions; // every action the compositor knows
    bool other_profile = false;    // an appearance profile to switch to and back
    bool spare_workspace = false;  // an empty workspace besides the tour's own
    bool scratchpad_free = false;  // none of the user's windows in the scratchpad
};
// Why `step` cannot run with what `offer` has, or "" when it can.
std::string skip_reason(const Step &step, const Offer &offer);

// `seconds` at `speed` times the usual pace, in milliseconds.
int scaled_ms(double seconds, double speed);
// How long a step takes at the usual pace: its waits and its hold, in seconds.
double step_seconds(const Step &step);

// Keys as Config::binding_keys gives them ("Super + Shift + minus") as a caption shows them
// ("Super + Shift + -").
std::string key_label(const std::string &keys);
// The caption's keys for `actions`, from `bindings` (each action's keys as `get actions` has
// them, "-" for none): the labels of those bound, joined by " / ", else "paw msg ACTION" for the
// first; "" for no actions.
std::string caption_keys(const std::vector<std::string> &actions,
                         const std::map<std::string, std::string> &bindings);

// The appearance profile to show off from `current`, of `profiles` in name order: its light or
// dark twin when there is one, else the next one, wrapping; "" when there is no other, or no
// `current` to come back to.
std::string other_profile(const std::vector<std::string> &profiles, const std::string &current);

// `text` quoted for sh.
std::string shell_quote(const std::string &text);
// What runs `script` with sh in a new window of `terminal` (its command line) titled `title`.
std::vector<std::string> terminal_command(const std::vector<std::string> &terminal,
                                          const std::string &title, const std::string &script);
// --client's command for sh, every {title} in it replaced by `title`, quoted.
std::string client_command(const std::string &command, const std::string &title);

// Whether `request` acts on the focused window, which then has to be one of the tour's.
bool acts_on_window(const std::string &request);
// Whether `request` acts on a monitor (its workspace, tiling, layout or overlays), which the tour
// sends as "output NAME REQUEST" so that it reaches its own.
bool acts_on_output(const std::string &request);
// `request` with each {NAME} replaced by `values`' NAME; throws for a name it does not have.
std::string expand(const std::string &request, const std::map<std::string, std::string> &values);

struct Options {
    bool list = false; // print the steps instead
    bool step = false; // wait for Enter between steps
    int from = 1;      // the first step to run, from 1
    double speed = 1;
    bool restore = true;
    std::string client; // what opens the windows instead of the terminal, for sh
};
// `paw tour`'s arguments, argv[1] being "tour"; throws std::runtime_error for a usage error.
Options parse_options(int argc, char **argv);

} // namespace paw::tour
