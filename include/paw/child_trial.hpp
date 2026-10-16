// SPDX-License-Identifier: GPL-3.0-or-later
// Running a user's Lua first in a child process, killed if it runs over a time limit: a Lua
// sandbox's instruction budget meters its Lua, but a call into a C function is one instruction
// however long it runs (a pattern that backtracks, (""):rep(2^60), table.move over 2^50 places).
// A configuration loads this way, and `paw import` runs a hyprland.lua so.
#pragma once

#include <functional>
#include <string>

namespace paw {
// The seconds the child gets: 3, or 10 in the sanitizers' build, which runs many times slower.
extern const int trial_seconds;

struct Trial {
    enum Outcome {
        Finished,    // in time, whether `work` returned or threw; or not tried, no child to be had
        OutOfMemory, // `work` threw std::bad_alloc
        TooLong,     // still running after trial_seconds, and killed
        Crashed,     // the child ended without saying how `work` went
    } outcome = Finished;
    std::string signal; // Crashed by a signal: its description
};

// Runs `work` in a child process forked for it, which must finish within trial_seconds, and says
// how that went; the caller then runs it for real, or not. The child leaves with _exit, which runs
// no destructors or exit handlers of the process it was forked from, and leaves no core file,
// which would be a copy of that process's memory. What `work` calls must be safe in a child
// forked from a process with threads: nothing that waits on a lock another thread may hold.
Trial try_in_child(const std::function<void()> &work);
} // namespace paw
