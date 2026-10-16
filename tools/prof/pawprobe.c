// SPDX-License-Identifier: GPL-3.0-or-later
// pawprobe: a sampling profiler and a heap logger to LD_PRELOAD into paw or paw-shell, for hosts
// without perf, valgrind or gdb. See docs/performance.md.
//
//   cc -O2 -g -shared -fPIC -pthread -o build/pawprobe.so tools/prof/pawprobe.c
//   LD_PRELOAD=$PWD/build/pawprobe.so PAWPROBE_DIR=$PWD/prof PAWPROBE_ONLY=paw,paw-shell
//       PAWPROBE_SKIP_ARG1=msg build/paw --headless
//
// In each process it profiles it writes, as DIR/EXE.PID.* (EXE.PID.2.* and so on should a
// process exec the same program again):
//   samples  CPU samples: ITIMER_PROF delivers SIGPROF to the thread that used the time, and
//            the handler writes its stack, from glibc's backtrace(): a header (see below), then
//            per sample the depth, the thread's id, CLOCK_MONOTONIC in ns and the addresses,
//            the interrupted instruction first and return addresses after it.
//   maps     /proc/self/maps, at start, every few seconds and at exit, for report.py to map
//            the addresses to objects.
//   threads  "TID\tNAME" for each thread seen while it ran.
//   log      every PAWPROBE_LOG_MS: CLOCK_MONOTONIC seconds, the live heap (mallinfo2's
//            uordblks) and mmapped allocations (hblkhd) in bytes, RSS in kB, open fds,
//            threads, CPU ms (all threads), and the main thread's voluntary and involuntary
//            context switches. heaptrend.py reads it.
//
// The environment:
//   PAWPROBE_DIR        where the files go (created; default ./pawprobe)
//   PAWPROBE_ONLY       comma-separated program names to probe; others are left alone
//   PAWPROBE_SKIP_ARG1  comma-separated first arguments to leave alone (msg: `paw msg`)
//   PAWPROBE_HZ         samples per second of CPU time (default 250; 0: no sampling). The
//                       kernel checks the timer on its tick, so more than CONFIG_HZ is not had.
//   PAWPROBE_DEPTH      frames kept per sample (default 64, at most 128)
//   PAWPROBE_LOG_MS     the log's period (default 200; 0: no log)
//
// Not with the ASan build: ASan's runtime has to be the first library loaded.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#define MAGIC "PAWPROB1"
#define MAX_DEPTH 128
#define MAPS_EVERY_MS 5000
#define MAX_THREADS 512

// The samples file starts with this, then holds records of a struct record and its frames.
struct header {
    char magic[8];
    uint32_t version, interval_us;
    uint64_t start_ns;
    uint32_t pid, reserved;
};
struct record {
    uint32_t depth, tid;
    uint64_t ns;
};

static pid_t probe_pid;
static char base[PATH_MAX]; // DIR/EXE.PID, the files' names without their suffix
static int samples_fd = -1, log_fd = -1;
static int depth = 64, log_ms = 200;
static bool sampling;
static atomic_bool stopping;
static pthread_t logger;
static bool logger_running;
static pid_t threads_seen[MAX_THREADS];
static char thread_names[MAX_THREADS][32];
static int threads_count;
static pthread_mutex_t threads_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static uintptr_t interrupted_pc(void *context) {
    const ucontext_t *uc = context;
#if defined(__x86_64__)
    return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    return (uintptr_t)uc->uc_mcontext.pc;
#else
#error "pawprobe knows the interrupted instruction's address on x86_64 and aarch64 only"
#endif
}

// Async-signal-safe: backtrace() was called once at start, so that it has loaded libgcc_s and
// does not allocate here.
static void on_sigprof(int signal, siginfo_t *info, void *context) {
    (void)signal;
    (void)info;
    int saved = errno;
    void *frames[MAX_DEPTH + 8];
    struct {
        struct record record;
        uint64_t addresses[MAX_DEPTH];
    } sample;
    int found = backtrace(frames, depth + 8);
    uintptr_t pc = interrupted_pc(context);
    // The first frames are this handler and the signal trampoline; the interrupted
    // instruction follows them. Should the unwinder not get past the trampoline, the sample
    // is that instruction alone.
    int first = -1;
    for (int i = 0; i < found && i < 8; ++i) {
        if ((uintptr_t)frames[i] == pc) {
            first = i;
            break;
        }
    }
    uint32_t n = 0;
    sample.addresses[n++] = pc;
    if (first >= 0) {
        for (int i = first + 1; i < found && n < (uint32_t)depth; ++i)
            sample.addresses[n++] = (uintptr_t)frames[i];
    }
    sample.record = (struct record){.depth = n, .tid = (uint32_t)syscall(SYS_gettid),
                                    .ns = now_ns()};
    size_t size = sizeof sample.record + n * sizeof *sample.addresses;
    // O_APPEND: one write per sample, so samples from threads at once do not interleave.
    ssize_t written = write(samples_fd, &sample, size);
    (void)written;
    errno = saved;
}

// Reads a small file into `buffer` without allocating; returns its length, or -1.
static ssize_t read_file(const char *path, char *buffer, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t used = 0;
    ssize_t got;
    while (used + 1 < size && (got = read(fd, buffer + used, size - used - 1)) != 0) {
        if (got < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        used += (size_t)got;
    }
    close(fd);
    buffer[used] = '\0';
    return (ssize_t)used;
}

static long long status_field(const char *status, const char *name) {
    const char *at = strstr(status, name);
    return at ? atoll(at + strlen(name)) : -1;
}

static int count_fds(void) {
    int fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    char buffer[4096];
    int count = 0;
    long got;
    while ((got = syscall(SYS_getdents64, fd, buffer, sizeof buffer)) > 0) {
        for (long at = 0; at < got;) {
            struct dirent64 *entry = (struct dirent64 *)(buffer + at);
            if (entry->d_name[0] != '.')
                ++count;
            at += entry->d_reclen;
        }
    }
    close(fd);
    return count - 1; // the descriptor reading the directory
}

static void write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        data += written;
        size -= (size_t)written;
    }
}

// Copies /proc/self/maps to BASE.maps, through a temporary file so that a reader never sees
// half of it.
static void save_maps(void) {
    char path[PATH_MAX + 16], temporary[PATH_MAX + 16];
    snprintf(path, sizeof path, "%s.maps", base);
    snprintf(temporary, sizeof temporary, "%s.maps.new", base);
    int in = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return;
    int out = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out < 0) {
        close(in);
        return;
    }
    char buffer[16384];
    ssize_t got;
    while ((got = read(in, buffer, sizeof buffer)) != 0) {
        if (got < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        write_all(out, buffer, (size_t)got);
    }
    close(in);
    close(out);
    rename(temporary, path);
}

// Notes the names of the threads running now, and writes every one seen so far to
// BASE.threads.
static void save_threads(void) {
    pthread_mutex_lock(&threads_lock);
    int dir = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        char buffer[4096];
        long got;
        while ((got = syscall(SYS_getdents64, dir, buffer, sizeof buffer)) > 0) {
            for (long at = 0; at < got;) {
                struct dirent64 *entry = (struct dirent64 *)(buffer + at);
                at += entry->d_reclen;
                if (entry->d_name[0] == '.')
                    continue;
                pid_t tid = atoi(entry->d_name);
                char path[64], name[32];
                snprintf(path, sizeof path, "/proc/self/task/%d/comm", tid);
                if (read_file(path, name, sizeof name) <= 0)
                    continue;
                name[strcspn(name, "\n")] = '\0';
                int slot = 0;
                while (slot < threads_count && threads_seen[slot] != tid)
                    ++slot;
                if (slot == MAX_THREADS)
                    continue;
                threads_seen[slot] = tid;
                snprintf(thread_names[slot], sizeof thread_names[slot], "%s", name);
                if (slot == threads_count)
                    ++threads_count;
            }
        }
        close(dir);
    }
    char path[PATH_MAX + 16];
    snprintf(path, sizeof path, "%s.threads", base);
    int out = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (out >= 0) {
        for (int i = 0; i < threads_count; ++i) {
            char line[64];
            int length = snprintf(line, sizeof line, "%d\t%s\n", threads_seen[i], thread_names[i]);
            write_all(out, line, (size_t)length);
        }
        close(out);
    }
    pthread_mutex_unlock(&threads_lock);
}

static void log_line(void) {
    struct mallinfo2 heap = mallinfo2();
    char status[8192];
    long long rss = -1, threads = -1, voluntary = -1, involuntary = -1;
    if (read_file("/proc/self/status", status, sizeof status) > 0) {
        rss = status_field(status, "VmRSS:");
        threads = status_field(status, "Threads:");
        voluntary = status_field(status, "\nvoluntary_ctxt_switches:");
        involuntary = status_field(status, "nonvoluntary_ctxt_switches:");
    }
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    long long cpu_ms = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000LL +
                       (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000;
    uint64_t now = now_ns();
    char line[256];
    int length = snprintf(line, sizeof line, "%llu.%06llu\t%zu\t%zu\t%lld\t%d\t%lld\t%lld\t%lld\t%lld\n",
                          (unsigned long long)(now / 1000000000u),
                          (unsigned long long)(now % 1000000000u / 1000), heap.uordblks,
                          heap.hblkhd, rss, count_fds(), threads, cpu_ms, voluntary, involuntary);
    write_all(log_fd, line, (size_t)length);
}

static void *log_loop(void *unused) {
    (void)unused;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    long since_maps = 0;
    while (!atomic_load(&stopping)) {
        log_line();
        if ((since_maps += log_ms) >= MAPS_EVERY_MS) {
            since_maps = 0;
            save_maps();
            save_threads();
        }
        next.tv_nsec += (long)log_ms * 1000000;
        while (next.tv_nsec >= 1000000000) {
            next.tv_nsec -= 1000000000;
            ++next.tv_sec;
        }
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR) {
        }
    }
    return NULL;
}

static int env_int(const char *name, int fallback) {
    const char *value = getenv(name);
    return value && *value ? atoi(value) : fallback;
}

// Whether `word` is one of the comma-separated `list`.
static bool listed(const char *list, const char *word) {
    size_t length = strlen(word);
    for (const char *at = list; *at;) {
        size_t span = strcspn(at, ",");
        if (span == length && !strncmp(at, word, length))
            return true;
        at += span + (at[span] == ',');
    }
    return false;
}

static bool wanted(char *program, size_t size) {
    char exe[PATH_MAX];
    ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (length <= 0)
        return false;
    exe[length] = '\0';
    char *deleted = strstr(exe, " (deleted)");
    if (deleted)
        *deleted = '\0';
    const char *name = strrchr(exe, '/') ? strrchr(exe, '/') + 1 : exe;
    size_t length_kept = strlen(name) < size ? strlen(name) : size - 1;
    memcpy(program, name, length_kept);
    program[length_kept] = '\0';
    const char *only = getenv("PAWPROBE_ONLY");
    if (only && *only && !listed(only, name))
        return false;
    const char *skip = getenv("PAWPROBE_SKIP_ARG1");
    if (skip && *skip) {
        char cmdline[4096];
        ssize_t got = read_file("/proc/self/cmdline", cmdline, sizeof cmdline);
        size_t first = strlen(cmdline) + 1;
        if (got > 0 && first < (size_t)got && listed(skip, cmdline + first))
            return false;
    }
    return true;
}

// Opens DIR/PROGRAM.PID.samples, or .2, .3 ... if an earlier image of this process left one.
static int open_samples(const char *dir, const char *program) {
    for (int attempt = 1; attempt < 100; ++attempt) {
        if (attempt == 1)
            snprintf(base, sizeof base, "%s/%s.%d", dir, program, (int)probe_pid);
        else
            snprintf(base, sizeof base, "%s/%s.%d.%d", dir, program, (int)probe_pid, attempt);
        char path[PATH_MAX + 16];
        snprintf(path, sizeof path, "%s.samples", base);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_CLOEXEC, 0644);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    return -1;
}

__attribute__((constructor)) static void pawprobe_start(void) {
    char program[256];
    if (!wanted(program, sizeof program))
        return;
    probe_pid = getpid();
    const char *dir = getenv("PAWPROBE_DIR");
    if (!dir || !*dir)
        dir = "pawprobe";
    mkdir(dir, 0755);
    int hz = env_int("PAWPROBE_HZ", 250);
    depth = env_int("PAWPROBE_DEPTH", 64);
    depth = depth < 1 ? 1 : depth > MAX_DEPTH ? MAX_DEPTH : depth;
    log_ms = env_int("PAWPROBE_LOG_MS", 200);
    samples_fd = open_samples(dir, program);
    if (samples_fd < 0) {
        fprintf(stderr, "pawprobe: cannot write to %s: %s\n", dir, strerror(errno));
        return;
    }
    uint32_t interval_us = hz > 0 ? (uint32_t)(1000000 / hz) : 0;
    struct header header = {.version = 1, .interval_us = interval_us, .start_ns = now_ns(),
                            .pid = (uint32_t)probe_pid};
    memcpy(header.magic, MAGIC, sizeof header.magic);
    write_all(samples_fd, (const char *)&header, sizeof header);
    save_maps();
    save_threads();

    if (log_ms > 0) {
        char path[PATH_MAX + 16];
        snprintf(path, sizeof path, "%s.log", base);
        log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    }
    if (log_fd >= 0) {
        static const char columns[] =
            "# t\theap\tmmap\trss_kb\tfds\tthreads\tcpu_ms\tvcsw\tivcsw\n";
        write_all(log_fd, columns, sizeof columns - 1);
        // The logger takes no signals but SIGPROF: a compositor that waits for SIGTERM or
        // SIGCHLD on a signalfd blocks them in its main thread only, and a thread that left
        // them unblocked would be given them instead.
        sigset_t all, old;
        sigfillset(&all);
        sigdelset(&all, SIGPROF);
        pthread_sigmask(SIG_BLOCK, &all, &old);
        logger_running = !pthread_create(&logger, NULL, log_loop, NULL);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
        if (logger_running)
            pthread_setname_np(logger, "pawprobe");
    }

    if (interval_us > 0) {
        void *prime[2];
        backtrace(prime, 2);
        struct sigaction action = {.sa_sigaction = on_sigprof,
                                   .sa_flags = SA_SIGINFO | SA_RESTART};
        sigemptyset(&action.sa_mask);
        sigaction(SIGPROF, &action, NULL);
        struct timeval every = {(time_t)(interval_us / 1000000),
                                (suseconds_t)(interval_us % 1000000)};
        struct itimerval timer = {.it_interval = every, .it_value = every};
        sampling = !setitimer(ITIMER_PROF, &timer, NULL);
    }
}

__attribute__((destructor)) static void pawprobe_stop(void) {
    // A child forked without exec that calls exit() runs this too; it is not the probed one.
    if (samples_fd < 0 || getpid() != probe_pid)
        return;
    if (sampling) {
        struct itimerval off = {0};
        setitimer(ITIMER_PROF, &off, NULL);
        sampling = false;
    }
    atomic_store(&stopping, true);
    if (logger_running) {
        pthread_join(logger, NULL);
        logger_running = false;
    }
    if (log_fd >= 0)
        log_line();
    save_maps();
    save_threads();
}
