// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/lid.h"
#include <string.h>

bool sh_output_built_in(const char *connector) {
    static const char *const kinds[] = {"eDP", "LVDS", "DSI"};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(*kinds); ++i) {
        size_t length = strlen(kinds[i]);
        if (!strncmp(connector, kinds[i], length) &&
            (connector[length] == '-' || connector[length] == '\0'))
            return true;
    }
    return false;
}

bool sh_lid_turns_off(enum sh_lid_mode mode, bool lid_closed, bool built_in, bool external_on) {
    return mode == SH_LID_CLAMSHELL && lid_closed && built_in && external_on;
}
