// SPDX-License-Identifier: GPL-3.0-or-later
// A user's Lua run first in a child process with a limit of processor time; see
// paw/child_trial.hpp.
#include "paw/child_trial.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PAW_TRIAL_SANITIZED 1
#endif
#endif

namespace paw {
#if defined(__SANITIZE_ADDRESS__) || defined(PAW_TRIAL_SANITIZED)
// The shipped configuration loads in about a second there (0.05 s in the normal build), with
// tools/check-all.sh's slow unwinding of every allocation.
const int trial_seconds = 10;
const int trial_wall_seconds = 120;
#else
const int trial_seconds = 3;
const int trial_wall_seconds = 30;
#endif

namespace {
// How long a child that said how `work` went, or was killed, may take to end before it is left
// to whatever reaps the caller's other children.
constexpr auto reap_limit = std::chrono::seconds(3);

// Reaps `child`, which has ended or is about to, waiting at most reap_limit; true once reaped.
bool reap(pid_t child, int &status, rusage &usage) {
    const auto give_up = std::chrono::steady_clock::now() + reap_limit;
    auto pause = std::chrono::microseconds(50);
    for (;;) {
        pid_t done = ::wait4(child, &status, WNOHANG, &usage);
        if (done == child)
            return true;
        if (done < 0 && errno != EINTR)
            return false;
        if (std::chrono::steady_clock::now() >= give_up)
            return false;
        ::usleep(static_cast<useconds_t>(pause.count()));
        pause = std::min(pause * 2, std::chrono::microseconds(10000));
    }
}

double seconds_of(const timeval &time) {
    return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) / 1e6;
}
} // namespace

Trial try_in_child(const std::function<void()> &work, int cpu_seconds, int wall_seconds) {
    int pipe_ends[2];
    if (::pipe2(pipe_ends, O_CLOEXEC) != 0)
        return {};
    const pid_t parent = ::getpid();
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipe_ends[0]);
        ::close(pipe_ends[1]);
        return {};
    }
    if (child == 0) {
        ::close(pipe_ends[0]);
        ::prctl(PR_SET_PDEATHSIG, SIGKILL);
        const rlimit no_core{0, 0};
        ::setrlimit(RLIMIT_CORE, &no_core);
        // SIGXCPU at the limit, which ends it, and SIGKILL a second later should it not. The
        // parent may have blocked or handled it, as the compositor does the signals it watches.
        ::signal(SIGXCPU, SIG_DFL);
        sigset_t processor;
        sigemptyset(&processor);
        sigaddset(&processor, SIGXCPU);
        ::sigprocmask(SIG_UNBLOCK, &processor, nullptr);
        const rlimit processor_time{static_cast<rlim_t>(cpu_seconds),
                                    static_cast<rlim_t>(cpu_seconds) + 1};
        ::setrlimit(RLIMIT_CPU, &processor_time);
        if (::getppid() != parent)
            _exit(1);
        char outcome = 'y';
        try {
            work();
        } catch (const std::bad_alloc &) {
            outcome = 'm';
        } catch (...) {
            outcome = 'n';
        }
        ssize_t written;
        do
            written = ::write(pipe_ends[1], &outcome, 1);
        while (written < 0 && errno == EINTR);
        _exit(0);
    }
    ::close(pipe_ends[1]);
    char outcome = 0;
    bool late = false, unchecked = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(wall_seconds);
    for (;;) {
        auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline -
                                                                 std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            late = true;
            break;
        }
        pollfd ready{pipe_ends[0], POLLIN, 0};
        int count = ::poll(&ready, 1, static_cast<int>(left.count()));
        if (count < 0 && errno != EINTR) {
            unchecked = true;
            break;
        }
        if (count <= 0)
            continue;
        // A byte, or none as the child ended without one: killed at its limit, or crashed.
        if (::read(pipe_ends[0], &outcome, 1) >= 0 || errno != EINTR)
            break;
    }
    ::close(pipe_ends[0]);
    if (!outcome)
        ::kill(child, SIGKILL);
    int status = 0;
    rusage usage{};
    const bool reaped = reap(child, status, usage);
    if (outcome == 'y' || outcome == 'n' || unchecked)
        return {};
    if (outcome == 'm')
        return {Trial::OutOfMemory, {}};
    if (late)
        return {Trial::TooLong, {}, wall_seconds, false};
    // Over its processor time: SIGXCPU, or SIGKILL at the hard limit a second later.
    const int ended_by = reaped && WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    const double used = seconds_of(usage.ru_utime) + seconds_of(usage.ru_stime);
    if (ended_by == SIGXCPU || (ended_by == SIGKILL && used >= cpu_seconds))
        return {Trial::TooLong, {}, cpu_seconds, true};
    return {Trial::Crashed, ended_by ? strsignal(ended_by) : ""};
}
} // namespace paw
