// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw cat [on|off|toggle]`: the cat action from the command line, which shows or hides the
 * desktop cat (src/compositor/cat.c), and what the cat is up to then. */
#include "paw/cli.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace paw::cli {

std::string cat_said(const std::vector<std::vector<std::string>> &rows) {
    if (rows.empty() || rows[0].size() < 9)
        return "The cat said nothing.";
    const auto &cat = rows[0];
    if (cat[0] != "on")
        return "The cat is away.";
    std::string where = cat[2] == "-" ? "" : " on " + cat[2];
    const std::string &why = cat[1];
    if (why == "locked")
        return "The cat is out" + where + ", hidden while the screen is locked.";
    if (why == "fullscreen")
        return "The cat is out" + where + ", hidden behind a fullscreen window.";
    if (why == "display_off")
        return "The cat is out" + where + ", hidden while its monitor is off.";
    if (why == "no_output")
        return "The cat is out, waiting for a monitor.";
    if (cat[5] == "sleep")
        return "The cat is asleep" + where + ".";
    if (cat[5] == "purr") // curled up as it sleeps, or sitting up
        return (cat[6].starts_with("sleep") ? "The cat purrs in its sleep" : "The cat is purring") +
               where + ".";
    if (cat[8] != "-" && cat[8] != "floor")
        return "The cat is out" + where + ", on " + cat[8] + ".";
    return "The cat is out" + where + ".";
}

int cat(int argc, char **argv) {
    if (argc > 3 || (argc == 3 && argv[2] != std::string_view("on") &&
                     argv[2] != std::string_view("off") && argv[2] != std::string_view("toggle")))
        throw std::runtime_error("usage: paw cat [on|off|toggle]");
    auto reply = request(argc == 3 ? std::string("cat ") + argv[2] : "cat");
    if (!reply.ok) {
        std::cerr << "paw: " << reply.body;
        return 1;
    }
    std::cout << cat_said(query("cat")) << '\n';
    return 0;
}

} // namespace paw::cli
