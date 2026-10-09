// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Where the machine's power comes from, for the idle timeouts on battery. */
#include <stdbool.h>

/* Whether the machine runs on battery, from the power supplies under `sysfs` (normally "/sys"):
 * a battery of its own (type Battery, not a peripheral's with scope Device) discharging, and no
 * mains or USB supply online. False when it cannot tell, as on a machine without a battery. */
bool sh_on_battery(const char *sysfs);
