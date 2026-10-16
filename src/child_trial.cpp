// SPDX-License-Identifier: GPL-3.0-or-later
// A user's Lua run first in a child process with a time limit; see paw/child_trial.hpp.
#include "paw/child_trial.hpp"

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
// tools/check-all.sh's slow unwinding of every allocation, on a loaded machine too.
const int trial_seconds = 10;
#else
const int trial_seconds = 3;
#endif

Trial try_in_child(const std::function<void()> &work) {
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(trial_seconds);
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
        if (::read(pipe_ends[0], &outcome, 1) >= 0 || errno != EINTR)
            break;
    }
    ::close(pipe_ends[0]);
    if (!outcome)
        ::kill(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (outcome == 'y' || outcome == 'n' || unchecked)
        return {};
    if (outcome == 'm')
        return {Trial::OutOfMemory, {}};
    if (late)
        return {Trial::TooLong, {}};
    return {Trial::Crashed, WIFSIGNALED(status) ? strsignal(WTERMSIG(status)) : ""};
}
} // namespace paw
