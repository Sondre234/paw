// SPDX-License-Identifier: GPL-3.0-or-later
/* What the subcommands share (src/cli/control_client.cpp): a reply split into rows, and where
 * paw keeps its state. */
#include "check.h"
#include "paw/cli.hpp"

#include <cstdlib>

int main() {
    auto rows = paw::cli::rows("a\tb\tc\nd\n\te\t\n");
    CHECK(rows.size() == 3, "%zu rows", rows.size());
    CHECK(rows[0] == (std::vector<std::string>{"a", "b", "c"}));
    CHECK(rows[1] == (std::vector<std::string>{"d"}));
    CHECK(rows[2] == (std::vector<std::string>{"", "e", ""}), "empty fields are kept");
    CHECK(paw::cli::rows("").empty());
    CHECK(paw::cli::rows("x").size() == 1, "a last line without a newline counts");

    setenv("XDG_STATE_HOME", "/state", 1);
    CHECK(paw::cli::state_directory() == "/state/paw");
    setenv("XDG_STATE_HOME", "relative", 1);
    setenv("HOME", "/home/someone", 1);
    CHECK(paw::cli::state_directory() == "/home/someone/.local/state/paw",
          "a relative XDG_STATE_HOME is ignored, as the specification says");
    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    CHECK(paw::cli::state_directory().empty());

    unsetenv("PAW_SOCKET");
    bool refused = false;
    try {
        paw::cli::request("get stats");
    } catch (const std::runtime_error &) {
        refused = true;
    }
    CHECK(refused, "a request outside a session throws");
    return failures != 0;
}
