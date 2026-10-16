// SPDX-License-Identifier: GPL-3.0-or-later
// try_in_child: what runs past its processor time is stopped, what only waits is not until the
// clock's backstop, and each way it can end is told apart.
#include "paw/child_trial.hpp"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include "check.h"

namespace {
double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}
void spin() {
    volatile unsigned long count = 0;
    for (;;)
        count = count + 1;
}
} // namespace

int main() {
    // Busy past one second of processor time: stopped, as over that limit.
    auto start = std::chrono::steady_clock::now();
    auto trial = paw::try_in_child(spin, 1, 60);
    CHECK(trial.outcome == paw::Trial::TooLong && trial.processor_time && trial.seconds == 1,
          "outcome %d, %d seconds, processor time %d", trial.outcome, trial.seconds,
          trial.processor_time);
    CHECK(seconds_since(start) < 50, "the backstop stopped it, after %.1f s",
          seconds_since(start));

    // Waiting longer than that on the clock, as on a machine busy with other work, while using
    // next to no processor time: it finishes.
    trial = paw::try_in_child([] { ::usleep(2'000'000); }, 1, 60);
    CHECK(trial.outcome == paw::Trial::Finished, "a child that waited 2 s ended %d",
          trial.outcome);

    // Waiting for ever: the clock's backstop stops it.
    start = std::chrono::steady_clock::now();
    trial = paw::try_in_child([] { ::pause(); }, 1, 2);
    CHECK(trial.outcome == paw::Trial::TooLong && !trial.processor_time && trial.seconds == 2,
          "outcome %d, %d seconds, processor time %d", trial.outcome, trial.seconds,
          trial.processor_time);
    CHECK(seconds_since(start) >= 1.9, "stopped after %.1f s", seconds_since(start));

    // The compositor blocks the signals it watches; the child is stopped all the same.
    sigset_t processor, before;
    sigemptyset(&processor);
    sigaddset(&processor, SIGXCPU);
    sigprocmask(SIG_BLOCK, &processor, &before);
    auto ignored = std::signal(SIGXCPU, SIG_IGN);
    trial = paw::try_in_child(spin, 1, 60);
    CHECK(trial.outcome == paw::Trial::TooLong && trial.processor_time,
          "with SIGXCPU blocked and ignored: outcome %d", trial.outcome);
    std::signal(SIGXCPU, ignored);
    sigprocmask(SIG_SETMASK, &before, nullptr);

    // How `work` ended otherwise.
    CHECK(paw::try_in_child([] {}).outcome == paw::Trial::Finished);
    CHECK(paw::try_in_child([] { throw std::runtime_error("refused"); }).outcome ==
              paw::Trial::Finished,
          "an exception is left to running it for real");
    CHECK(paw::try_in_child([] { throw std::bad_alloc(); }).outcome == paw::Trial::OutOfMemory);
    trial = paw::try_in_child([] { std::abort(); });
    CHECK(trial.outcome == paw::Trial::Crashed && !trial.signal.empty(), "outcome %d, '%s'",
          trial.outcome, trial.signal.c_str());
    return failures != 0;
}
