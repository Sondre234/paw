// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw cat`: what it says of the cat `get cat` describes, and its usage. */
#include "check.h"
#include "paw/cli.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

static std::string said(const std::string &line) { return paw::cli::cat_said(paw::cli::rows(line)); }

int main() {
    CHECK(said("off\toff\t-\t0\t0\t-\t-\tginger\t-") == "The cat is away.");
    CHECK(said("on\tshown\tDP-1\t900\t400\tsit\tsit\tginger\tfoot") ==
          "The cat is out on DP-1, on foot.");
    CHECK(said("on\tshown\tDP-1\t900\t1032\twalk\twalk_1\tginger\tfloor") ==
          "The cat is out on DP-1.");
    CHECK(said("on\twaiting\t-\t0\t0\t-\t-\tblack\t-") == "The cat is out.");
    CHECK(said("on\tshown\tHDMI-A-1\t900\t400\tsleep\tsleep_1\tcream\tfoot") ==
          "The cat is asleep on HDMI-A-1.");
    CHECK(said("on\tlocked\tDP-1\t900\t400\tsit\tsit\tginger\tfoot") ==
          "The cat is out on DP-1, hidden while the screen is locked.");
    CHECK(said("on\tfullscreen\tDP-1\t900\t400\tsit\tsit\tginger\tfoot") ==
          "The cat is out on DP-1, hidden behind a fullscreen window.");
    CHECK(said("on\tdisplay_off\tDP-1\t900\t400\tsleep\tsleep_2\tginger\t-") ==
          "The cat is out on DP-1, hidden while its monitor is off.");
    CHECK(said("on\tno_output\t-\t0\t0\t-\t-\tginger\t-") ==
          "The cat is out, waiting for a monitor.");
    CHECK(said("") == "The cat said nothing.");

    unsetenv("PAW_SOCKET");
    char name[] = "paw", sub[] = "cat", wrong[] = "maybe", on[] = "on";
    char *bad[] = {name, sub, wrong, nullptr};
    bool refused = false;
    try {
        paw::cli::cat(3, bad);
    } catch (const std::runtime_error &error) {
        refused = std::string(error.what()) == "usage: paw cat [on|off|toggle]";
    }
    CHECK(refused, "an unknown word is a usage error");
    char *good[] = {name, sub, on, nullptr};
    refused = false;
    try {
        paw::cli::cat(3, good);
    } catch (const std::runtime_error &error) {
        refused = std::string(error.what()).find("PAW_SOCKET") != std::string::npos;
    }
    CHECK(refused, "outside a session it says why");
    return failures != 0;
}
