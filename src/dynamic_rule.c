// SPDX-License-Identifier: GPL-3.0-or-later
/* Dynamic window rules: holding a property of a window to what the rules matching it now
 * decide, and giving it back as they stop. */
#include "paw/dynamic_rule.h"

int sh_held_value_step(struct sh_held_value *value, int want, bool current) {
    want = want < 0 ? -1 : want != 0;
    if (want == value->decided)
        return -1;
    int previous = value->decided;
    value->decided = want;
    // Changed by hand while held: the window's own value now.
    bool changed = value->held && previous >= 0 && current != (previous != 0);
    if (want >= 0) {
        if (!value->held || changed)
            value->own = current;
        value->held = true;
        return want == current ? -1 : want;
    }
    if (!value->held)
        return -1;
    value->held = false;
    if (changed || value->own == current)
        return -1;
    return value->own;
}
