// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* What a laptop's lid does to its built-in panel. */
#include <stdbool.h>

#include "paw/backend.h"

/* Whether a connector names a laptop's built-in panel: eDP, LVDS or DSI, as "eDP-1". */
bool sh_output_built_in(const char *connector);

/* Whether a built-in panel is to be off: in clamshell mode, the lid closed while a monitor that
 * is not built in is in the layout to show the desktop. Without one, nothing changes (logind
 * suspends the machine as the lid closes, or the panel stays the only screen). */
bool sh_lid_turns_off(enum sh_lid_mode mode, bool lid_closed, bool built_in, bool external_on);
