// SPDX-License-Identifier: GPL-3.0-or-later
/* What a laptop's lid does to its built-in panel: which connectors are built in, and when
 * clamshell mode turns the panel off. */
#include "paw/lid.h"
#include <stdio.h>
#include "check.h"

int main(void) {
    const char *built_in[] = {"eDP-1", "eDP-2", "LVDS-1", "DSI-1", "eDP"};
    for (size_t i = 0; i < sizeof(built_in) / sizeof(*built_in); ++i)
        CHECK(sh_output_built_in(built_in[i]), "%s is a laptop's panel", built_in[i]);
    const char *external[] = {"DP-1", "HDMI-A-1", "DVI-D-1", "VGA-1", "HEADLESS-1", "WL-1",
                              "eDPX-1", "DP-eDP-1", "", "edp-1"};
    for (size_t i = 0; i < sizeof(external) / sizeof(*external); ++i)
        CHECK(!sh_output_built_in(external[i]), "%s is not a laptop's panel", external[i]);

    // Clamshell: off only with the lid closed and another monitor to show the desktop.
    CHECK(sh_lid_turns_off(SH_LID_CLAMSHELL, true, true, true), "closed, docked");
    CHECK(!sh_lid_turns_off(SH_LID_CLAMSHELL, true, true, false), "closed, nothing else");
    CHECK(!sh_lid_turns_off(SH_LID_CLAMSHELL, false, true, true), "open, docked");
    CHECK(!sh_lid_turns_off(SH_LID_CLAMSHELL, true, false, true), "an external monitor");
    CHECK(!sh_lid_turns_off(SH_LID_IGNORE, true, true, true), "the lid ignored");
    if (failures)
        return 1;
    puts("lid passed");
    return 0;
}
