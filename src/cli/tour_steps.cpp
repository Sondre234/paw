// SPDX-License-Identifier: GPL-3.0-or-later
/* The steps `paw tour` takes, in order, one a line:

       TITLE | SENTENCE | KEYS | REQUESTS | SECONDS [| needs ACTION ...]

   The caption shows the title, the sentence and the keys of the actions KEYS names, as the user
   has them bound ("-" for none; the sentence names no keys, which the user may have moved).
   REQUESTS are control requests separated by ";" ("-" for none), run one after the other, those
   for a monitor on the tour's own, those for the focused window once one of the tour's has the
   focus; among them the tour's own:

       wait SECONDS     a pause, at the tour's speed
       open N           N more windows of the tour's
       tiling on|off    the tour's monitor tiling, or floating, whichever it is now
       focus other      another of the tour's windows focused
       close windows    the tour's windows closed, one after the other

   and these in any request: {spare} (an empty workspace besides the tour's), {profile} (the
   appearance profile in use), {other_profile} (another one) and {night_light} (the action that
   puts night light back as it was). SECONDS is how long the step stays after its requests. A step
   for an action the compositor may not have yet ends with "needs ACTION ...", and is skipped
   without it; so is one that uses {spare} or {other_profile} without a second empty workspace or
   another profile, and one that uses the scratchpad while windows of the user's are in it.
   Every step leaves the windows as a later one can start from: those that need tiling or
   floating say so. */
#include "paw/tour.hpp"

namespace paw::tour {

const char *const steps_table = R"(
Welcome to paw | Floating windows first and tiling when you want it: a minute of what paw does, then your desktop goes back as it was. | - | - | 4
Windows that place themselves | New windows open where they cover the least, and a key lays a monitor's windows out in a grid. | tile | tiling off; wait 0.6; tile | 3
Snapping | Windows snap to halves and quarters of the screen, and Snap Assist offers the other windows for the space beside. | snap_cycle_left snap_cycle_up | tiling off; snap_left; wait 1.6; overview_confirm; wait 1.6; maximize; wait 1.2; restore | 1.5
Tiling | One key tiles the monitor, splitting the screen between its windows as Hyprland's dwindle layout does. | toggle_tiling | tiling on | 3.5
Tiling layouts | Each workspace tiles its own way: a master and a stack, a spiral, or one window at a time. | layout_next | tiling on; layout_master; wait 1.8; layout_spiral; wait 1.8; layout_monocle; wait 1.8; layout_dwindle | 1
The scrolling layout | Windows stand in columns on a strip wider than the screen, as in niri, and the view glides after the focus. | layout_scroll | tiling on; layout_scroll; wait 1.4; scroll_left; wait 1.1; scroll_left; wait 1.1; scroll_right; wait 1.1; scroll_right; wait 1.4; layout_dwindle | 0.8
The overview | Every window of the workspace side by side to pick from, and typing finds one by its title. | toggle_overview | toggle_overview; wait 1.6; overview filter tour 2; wait 1.6; overview filter; wait 0.8; overview_cancel | 1
The window switcher | Every window on every workspace a key away, with a picture of each. | switcher | switcher; wait 0.9; switcher; wait 0.9; switcher; wait 1.2; switcher_cancel | 0.8
Tab groups | A window becomes a group, and those that open while it has the focus join it as tabs of one tile. | group_toggle group_next | tiling on; group_toggle; wait 0.6; open 1; wait 1.2; group_next; wait 1; group_next; wait 1; group_toggle | 1.2
The scratchpad | A window hides in the scratchpad, and drops down over everything again like a quake console. | move_to_scratchpad scratchpad_show | tiling on; move_to_scratchpad; wait 1.2; scratchpad_show; wait 1.6; scratchpad_show; wait 1; scratchpad_show; wait 1.4; toggle_floating | 0.8
Workspaces | Each monitor has workspaces of its own, and a key goes back to the one shown before. | workspace_next workspace_back | workspace {spare}; wait 1.5; workspace_back | 1.5
Sticky windows | A sticky window stays on every workspace of its monitor, over the others. | toggle_sticky | toggle_sticky; wait 0.8; workspace {spare}; wait 1.6; workspace_back; wait 0.8; toggle_sticky | 1
Appearance profiles | A profile from the panel or the command palette changes the whole look at once, crossfading from one to the next. | - | profile {other_profile}; wait 3.4; profile {profile} | 2
Night light | The screen warms in the evening and cools in the morning, on a schedule or by hand. | night_light_toggle | night_light_toggle; wait 3; {night_light} | 1
That's paw | There is more in docs/features.md, and paw tour --list names these steps; now your desktop goes back as it was. | - | close windows | 1.5
)";

} // namespace paw::tour
