// SPDX-License-Identifier: GPL-3.0-or-later
// Running a user's Lua first in a child process, killed if it runs over a limit of processor
// time: a Lua sandbox's instruction budget meters its Lua, but a call into a C function is one
// instruction however long it runs (a pattern that backtracks, (""):rep(2^60), table.move over
// 2^50 places). A configuration loads this way, and `paw import` runs a hyprland.lua so.
#pragma once

#include <functional>
#include <string>

namespace paw {
// The seconds of processor time the child gets: 3, or 10 in the sanitizers' build, which runs
// many times slower. Processor time rather than time on the clock, so that a machine busy with
// other work (a build, a login starting everything at once) gives a configuration as long as an
// idle one does.
extern const int trial_seconds;
// The seconds on the clock after which a child still running is killed however little processor
// time it used: 30, or 120 in the sanitizers' build. A backstop for one that waits rather than
// runs, such as on memory swapped out.
extern const int trial_wall_seconds;

struct Trial {
    enum Outcome {
        Finished,    // in time, whether `work` returned or threw; or not tried, no child to be had
        OutOfMemory, // `work` threw std::bad_alloc
        TooLong,     // ran over a limit, and killed
        Crashed,     // the child ended without saying how `work` went
    } outcome = Finished;
    std::string signal; // Crashed by a signal: its description
    // TooLong: the limit it ran over, in seconds, and whether those were of processor time
    // (else on the clock).
    int seconds = 0;
    bool processor_time = false;
};

// Runs `work` in a child process forked for it, which must finish within `cpu_seconds` of
// processor time and `wall_seconds` on the clock, and says how that went; the caller then runs
// it for real, or not. The child leaves with _exit, which runs no destructors or exit handlers of
// the process it was forked from, and leaves no core file, which would be a copy of that
// process's memory. What `work` calls must be safe in a child forked from a process with threads:
// nothing that waits on a lock another thread may hold.
//
// The child is reaped before this returns, unless it has not ended a few seconds after it was
// killed or said how `work` went (stuck in the kernel, on a device or a file system that does not
// answer): rather than waiting on it for ever, this leaves it to whatever reaps the caller's
// other children once it ends, such as the compositor's SIGCHLD handler.
Trial try_in_child(const std::function<void()> &work, int cpu_seconds = trial_seconds,
                   int wall_seconds = trial_wall_seconds);
} // namespace paw
