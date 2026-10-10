// SPDX-License-Identifier: GPL-3.0-or-later
/* Dynamic window rules: a property held to what the matching rules decide, and given back as
 * they stop, unless it was changed by hand meanwhile. */
#include "paw/dynamic_rule.h"
#include <stdio.h>
#include "check.h"

/* A window's property, and what the rules hold of it, as the compositor keeps them: a step's
 * answer is given to the window. */
struct window {
    bool value;
    struct sh_held_value held;
};

static struct window opened(bool value) {
    return (struct window){value, {.decided = -1}};
}

/* The rules decide `want`; returns what the step answered. */
static int decide(struct window *window, int want) {
    int give = sh_held_value_step(&window->held, want, window->value);
    if (give >= 0)
        window->value = give;
    return give;
}

int main(void) {
    // Nothing decided, nothing changes.
    struct window w = opened(false);
    CHECK(decide(&w, -1) == -1 && !w.value && !w.held.held, "no rule, no change");

    // A rule starts matching: the window is held to it, and given back what it had as it stops.
    CHECK(decide(&w, 1) == 1 && w.value && w.held.held, "the rule holds it");
    CHECK(decide(&w, 1) == -1 && w.value, "the same decision again changes nothing");
    CHECK(decide(&w, -1) == 0 && !w.value && !w.held.held, "given back as the rule stops");
    CHECK(decide(&w, -1) == -1 && !w.value, "nothing more to give back");

    // A rule deciding what the window has already holds it without a change, and lets it keep
    // that.
    w = opened(true);
    CHECK(decide(&w, 1) == -1 && w.value && w.held.held, "held to what it had");
    CHECK(decide(&w, -1) == -1 && w.value && !w.held.held, "its own value stays");

    // Changed by hand while held: it stays, also as the rule stops.
    w = opened(false);
    decide(&w, 1);
    w.value = false; // by hand
    CHECK(decide(&w, 1) == -1 && !w.value, "the rule does not fight a change by hand");
    CHECK(decide(&w, -1) == -1 && !w.value && !w.held.held, "the change by hand stays");
    // Matching anew, the rule holds it again.
    CHECK(decide(&w, 1) == 1 && w.value, "a rule matching anew holds it again");
    CHECK(decide(&w, -1) == 0 && !w.value, "and gives it back");

    // A decision that changes while held gives back what the window had before the first.
    w = opened(false);
    decide(&w, 1);
    CHECK(decide(&w, 0) == 0 && !w.value && w.held.held, "the new decision applies");
    CHECK(decide(&w, 1) == 1 && w.value, "and again");
    CHECK(decide(&w, -1) == 0 && !w.value, "what it had before the first is given back");
    w = opened(true);
    decide(&w, 0);
    decide(&w, 1);
    CHECK(decide(&w, -1) == -1 && w.value, "true before the first stays true");

    // Changed by hand, then decided otherwise: what it was changed to is its own.
    w = opened(false);
    decide(&w, 1);
    w.value = false; // by hand
    CHECK(decide(&w, 0) == -1 && !w.value, "already as the new decision has it");
    w.value = true; // by hand again, while the new decision holds it
    CHECK(decide(&w, -1) == -1 && w.value, "the change by hand stays as the rule stops");
    w = opened(true);
    decide(&w, 0);
    w.value = true; // by hand
    CHECK(decide(&w, 0) == -1 && w.value, "the same decision leaves the change by hand");
    CHECK(decide(&w, 1) == -1 && w.value, "a new decision it has already holds it");
    w.value = false; // by hand
    CHECK(decide(&w, -1) == -1 && !w.value, "its own value is the one changed by hand");

    // Decisions are booleans whatever number says them.
    w = opened(false);
    CHECK(decide(&w, 7) == 1 && w.value && w.held.decided == 1, "any true number decides 1");
    CHECK(decide(&w, -5) == 0 && !w.value && w.held.decided == -1, "any negative one nothing");

    if (failures)
        return 1;
    puts("dynamic rules passed");
    return 0;
}
