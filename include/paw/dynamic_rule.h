// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Dynamic window rules: what a rule with `dynamic = true` holds a window to while it matches the
 * window's title and app ID, and giving the window back what it had once the rule stops. */
#include <stdbool.h>

/* One property of a window the dynamic rules may hold (kept above, floating, sticky): what they
 * decided last (-1 for nothing), whether that decision holds the window, and the window's own
 * value to give back as they let go. Zeroed with decided -1 as the window opens. */
struct sh_held_value {
    int decided;
    bool held;
    bool own;
};

/* The dynamic rules now decide `want` (1 or 0, or -1 for nothing) for a window whose value is
 * `current`. Returns the value to give the window, or -1 to leave it as it is.
 *
 * A new decision holds the window to it, keeping what it had to give back; the same decision
 * again changes nothing, so that a value changed by hand while a rule holds it stays. As the
 * rules stop deciding, the window gets back what it had, unless it was changed by hand while
 * held, which it keeps. A decision that changes while held keeps what the window had before the
 * first, or what it was changed to by hand meanwhile. */
int sh_held_value_step(struct sh_held_value *value, int want, bool current);
