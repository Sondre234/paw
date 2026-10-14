// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw shake [WINDOW]`, `paw gravity` and `paw party`: the toys' actions, sent to the running
 * session as `paw msg` sends them. */
#include "paw/cli.hpp"

#include <stdexcept>
#include <string>

namespace paw::cli {

std::string toy_request(int argc, char **argv) {
    std::string name = argc > 1 ? argv[1] : "";
    if (name != "shake" && name != "gravity" && name != "party")
        throw std::runtime_error("no toy named " + name);
    // A shake takes the window's number; the compositor checks it.
    if (argc > (name == "shake" ? 3 : 2))
        throw std::runtime_error(name == "shake" ? "usage: paw shake [WINDOW]"
                                                 : "usage: paw " + name);
    return argc == 3 ? name + " " + argv[2] : name;
}

int toy(int argc, char **argv) {
    auto reply = request(toy_request(argc, argv));
    if (!reply.ok) {
        auto why = reply.body;
        if (why.rfind("error: ", 0) == 0)
            why.erase(0, 7);
        while (!why.empty() && why.back() == '\n')
            why.pop_back();
        throw std::runtime_error(why);
    }
    return 0;
}

} // namespace paw::cli
