// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw doctor [--plain] [--no-color]`: looks the running session over for what has gone wrong
 * in paw desktops before (the shell gone, the session bus dead, a compositor older than the paw
 * installed since, ...) and prints each finding with the command that fixes it. It only looks: a
 * get query to the compositor, files and /proc, `paw --version`, and D-Bus calls that change
 * nothing and start no service. */
#include "paw/autostart.hpp"
#include "paw/cli.hpp"
#include "paw/config.hpp"
#include "paw/doctor.hpp"

#if PAW_SDBUS_SYSTEMD
#include <systemd/sd-bus.h>
#elif PAW_SDBUS_ELOGIND
#include <elogind/sd-bus.h>
#elif PAW_SDBUS_BASU
#include <basu/sd-bus.h>
#endif
#if PAW_SDBUS_SYSTEMD || PAW_SDBUS_ELOGIND || PAW_SDBUS_BASU
#define PAW_DOCTOR_BUS 1
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace paw::cli {
namespace {
namespace fs = std::filesystem;
using doctor::Result;
using doctor::Status;

const char *const session_section = "Session", *const bus_section = "D-Bus";

std::string read_file(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}
std::string variable(const char *name) {
    const char *value = std::getenv(name);
    return value ? value : "";
}
std::string short_commit(const std::string &commit) { return commit.substr(0, 7); }
std::string local_time(std::time_t time) {
    char text[32];
    std::strftime(text, sizeof(text), "%F %T", std::localtime(&time));
    return text;
}
// The program `name` on PATH (or at that path, with a slash), "" when there is none.
std::string find_program(const std::string &name) {
    if (name.find('/') != std::string::npos)
        return access(name.c_str(), X_OK) == 0 ? name : "";
    std::istringstream path(variable("PATH"));
    for (std::string directory; std::getline(path, directory, ':');)
        if (auto candidate = fs::path(directory.empty() ? "." : directory) / name;
            access(candidate.c_str(), X_OK) == 0 && !fs::is_directory(candidate))
            return candidate.string();
    return {};
}

// What a program printed, its output and errors together, and how it exited; `ran` is false when
// it could not be started. One still running after `timeout_ms` is killed. It runs in the C
// locale, so that what it says can be read.
struct Output {
    bool ran = false;
    int status = -1;
    std::string text;
    std::string first_line() const { return text.substr(0, text.find('\n')); }
};
Output run(const std::vector<std::string> &command, int timeout_ms = 3000) {
    Output output;
    int ends[2];
    if (pipe2(ends, O_CLOEXEC) < 0)
        return output;
    std::vector<std::string> strings;
    for (char **entry = environ; entry && *entry; ++entry)
        if (!std::string_view(*entry).starts_with("LC_ALL="))
            strings.emplace_back(*entry);
    strings.emplace_back("LC_ALL=C");
    std::vector<char *> env, argv;
    for (auto &entry : strings)
        env.push_back(entry.data());
    env.push_back(nullptr);
    for (const auto &word : command)
        argv.push_back(const_cast<char *>(word.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, ends[1], 1);
    posix_spawn_file_actions_adddup2(&actions, ends[1], 2);
    pid_t pid = 0;
    int error = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), env.data());
    posix_spawn_file_actions_destroy(&actions);
    close(ends[1]);
    if (error) {
        close(ends[0]);
        return output;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buffer[4096];
    for (;;) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now())
                        .count();
        pollfd readable{ends[0], POLLIN, 0};
        int ready = left > 0 ? poll(&readable, 1, static_cast<int>(left)) : 0;
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0) {
            kill(pid, SIGKILL);
            break;
        }
        ssize_t count = read(ends[0], buffer, sizeof(buffer));
        if (count <= 0)
            break;
        output.text.append(buffer, static_cast<size_t>(count));
    }
    close(ends[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    output.ran = true;
    output.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return output;
}
// What `PROGRAM --version` says of the paw at `program`, without "paw "; "" when it says nothing.
std::string paw_version(const std::string &program) {
    auto output = run({program, "--version"});
    auto line = output.first_line();
    if (output.status != 0 || !line.starts_with("paw "))
        return {};
    return line.substr(4);
}

// A process of this user's, as /proc tells it.
struct Process {
    int pid = 0;
    doctor::ProcessStat stat;
    std::string exe{}; // where /proc/PID/exe links, "" when it cannot be read
    std::vector<std::string> argv{};
    // The name of the program it runs (not of a script it runs).
    std::string program() const { return fs::path(doctor::program_path(exe)).filename(); }
    bool running() const { return stat.state != 'Z' && stat.state != 'X'; }
    bool has_argument(const std::string &word) const {
        return std::find(argv.begin(), argv.end(), word) != argv.end();
    }
    doctor::Environment environment() const {
        return doctor::parse_environ(read_file("/proc/" + std::to_string(pid) + "/environ"));
    }
    // When it started, in seconds since the epoch.
    std::time_t started() const {
        static const auto boot = [] {
            std::istringstream stat(read_file("/proc/stat"));
            for (std::string word; stat >> word;)
                if (word == "btime" && stat >> word)
                    return std::stoll(word);
            return 0LL;
        }();
        return static_cast<std::time_t>(boot + static_cast<long long>(stat.start) /
                                                   sysconf(_SC_CLK_TCK));
    }
};
std::optional<Process> read_process(int pid) {
    auto base = "/proc/" + std::to_string(pid);
    auto stat = doctor::parse_stat(read_file(base + "/stat"));
    if (!stat)
        return std::nullopt;
    Process process{pid, *stat};
    char link[PATH_MAX];
    if (auto length = readlink((base + "/exe").c_str(), link, sizeof(link)); length > 0)
        process.exe.assign(link, static_cast<size_t>(length));
    process.argv = doctor::split_nul(read_file(base + "/cmdline"));
    return process;
}
// This user's processes.
std::vector<Process> user_processes() {
    std::vector<Process> found;
    std::error_code error;
    for (const auto &entry : fs::directory_iterator("/proc", error)) {
        auto name = entry.path().filename().string();
        struct stat info;
        if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos ||
            stat(entry.path().c_str(), &info) < 0 || info.st_uid != getuid())
            continue;
        if (auto process = read_process(std::stoi(name)))
            found.push_back(std::move(*process));
    }
    return found;
}

#if PAW_DOCTOR_BUS
constexpr uint64_t bus_timeout = 2'000'000; // microseconds
using Message = std::unique_ptr<sd_bus_message, sd_bus_message *(*)(sd_bus_message *)>;
// A connection to a bus that asks only what changes nothing, and has no service started for it.
class Bus {
  public:
    // The bus at `address`, or the system bus for "system"; null, with `error`, when there is none.
    static std::unique_ptr<Bus> connect(const std::string &address, std::string &error) {
        sd_bus *bus = nullptr;
        int result = 0;
        if (address == "system") {
            result = sd_bus_open_system(&bus);
        } else {
            result = sd_bus_new(&bus);
            if (result >= 0)
                result = sd_bus_set_address(bus, address.c_str());
            if (result >= 0)
                result = sd_bus_set_bus_client(bus, 1);
            if (result >= 0)
                result = sd_bus_start(bus);
        }
        if (result < 0) {
            error = std::strerror(-result);
            sd_bus_unref(bus);
            return nullptr;
        }
        return std::unique_ptr<Bus>(new Bus(bus));
    }
    ~Bus() { sd_bus_flush_close_unref(bus_); }
    // Whether the bus answers its own GetId.
    bool answers(std::string &error) {
        return call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId",
                    nullptr, error) != nullptr;
    }
    // The connection that owns `name`, "" for none.
    std::string owner(const std::string &name) {
        std::string error;
        auto reply = call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "GetNameOwner", name.c_str(), error);
        const char *owner = nullptr;
        if (!reply || sd_bus_message_read(reply.get(), "s", &owner) < 0 || !owner)
            return {};
        return owner;
    }
    // The process of `connection`, 0 when the bus does not say.
    int pid(const std::string &connection) {
        std::string error;
        auto reply = call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "GetConnectionUnixProcessID", connection.c_str(), error);
        uint32_t pid = 0;
        if (!reply || sd_bus_message_read(reply.get(), "u", &pid) < 0)
            return 0;
        return static_cast<int>(pid);
    }
    // Whether the bus would start a service for `name` (which this never asks it to).
    bool activatable(const std::string &name) {
        std::string error;
        auto reply = call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "ListActivatableNames", nullptr, error);
        if (!reply || sd_bus_message_enter_container(reply.get(), 'a', "s") < 0)
            return false;
        for (const char *found = nullptr; sd_bus_message_read(reply.get(), "s", &found) > 0;)
            if (found && name == found)
                return true;
        return false;
    }
    // A property of type u; nothing, with `error`, when it cannot be read.
    std::optional<uint32_t> unsigned_property(const char *destination, const char *path,
                                              const char *interface, const char *property,
                                              std::string &error) {
        auto reply = call(destination, path, "org.freedesktop.DBus.Properties", "Get",
                          interface, error, property);
        uint32_t value = 0;
        if (!reply)
            return std::nullopt;
        if (sd_bus_message_read(reply.get(), "v", "u", &value) < 0) {
            error = "no such property";
            return std::nullopt;
        }
        return value;
    }
    // The string a method without arguments returns; nothing, with `error`, when it fails.
    std::optional<std::string> string_call(const char *destination, const char *path,
                                           const char *interface, const char *method,
                                           std::string &error) {
        auto reply = call(destination, path, interface, method, nullptr, error);
        const char *value = nullptr;
        if (!reply || sd_bus_message_read(reply.get(), "s", &value) < 0 || !value)
            return std::nullopt;
        return value;
    }

  private:
    explicit Bus(sd_bus *bus) : bus_(bus) {}
    // Calls `method` with up to two string arguments, never starting a service for it.
    Message call(const char *destination, const char *path, const char *interface,
                 const char *method, const char *first, std::string &error,
                 const char *second = nullptr) {
        sd_bus_message *message = nullptr, *reply = nullptr;
        sd_bus_error failure = SD_BUS_ERROR_NULL;
        int result =
            sd_bus_message_new_method_call(bus_, &message, destination, path, interface, method);
        if (result >= 0)
            result = sd_bus_message_set_auto_start(message, 0);
        if (result >= 0 && first)
            result = sd_bus_message_append(message, "s", first);
        if (result >= 0 && second)
            result = sd_bus_message_append(message, "s", second);
        if (result >= 0)
            result = sd_bus_call(bus_, message, bus_timeout, &failure, &reply);
        if (result < 0)
            error = failure.message ? failure.message : std::strerror(-result);
        sd_bus_error_free(&failure);
        sd_bus_message_unref(message);
        return Message(result < 0 ? nullptr : reply, sd_bus_message_unref);
    }
    sd_bus *bus_;
};
#endif

// What the checks look at, gathered once.
struct Context {
    std::vector<Process> processes;
    // The control socket and what came back from it.
    std::string socket;
    bool connected = false, replied = false;
    std::string socket_error;
    // The compositor's process: the peer of $PAW_SOCKET, else this user's process whose program
    // is paw and that runs with --session (the script that starts the session may be called paw
    // too, and has --session in its command line, so its program is what tells them apart).
    std::optional<Process> compositor;
    std::string version; // what the compositor's program says it is, "" when unknown
    bool login = false;  // a login session (--session), which runs the shell as polkit agent
    fs::path config_path;
    std::optional<Config> config;
    std::string config_error;
    // session.log since the compositor started, in a login session that writes one.
    std::optional<std::string> log;
#if PAW_DOCTOR_BUS
    std::unique_ptr<Bus> bus; // the session bus, when it answers
#endif
    std::string bus_error;

    // Where the compositor's program is installed: PREFIX of PREFIX/bin/paw; "" when unknown.
    fs::path prefix() const {
        if (!compositor || compositor->exe.empty())
            return {};
        return fs::path(doctor::program_path(compositor->exe)).parent_path().parent_path();
    }
    const Process *process(int pid) const {
        for (const auto &process : processes)
            if (process.pid == pid)
                return &process;
        return nullptr;
    }
    // What `prefix` says last in session.log since the compositor started; "" when nothing.
    std::string logged(const std::string &prefix) const {
        return log ? doctor::last_line(*log, prefix) : std::string();
    }
    // "kill -HUP PID", the reload that starts the shell again.
    std::string reload() const {
        return compositor ? "kill -HUP " + std::to_string(compositor->pid) : "paw msg reload";
    }
};

// Sends `line` to the control socket at `path`, at most `timeout_ms` waiting for the reply, and
// notes which process listens there.
void ask_compositor(Context &context, const std::string &line, int timeout_ms = 2000) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (context.socket.size() >= sizeof(address.sun_path)) {
        context.socket_error = "the path is too long";
        return;
    }
    std::strcpy(address.sun_path, context.socket.c_str());
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        context.socket_error = std::strerror(errno);
        if (fd >= 0)
            close(fd);
        return;
    }
    context.connected = true;
    ucred peer{};
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.pid > 0)
        if (auto process = read_process(peer.pid); process && process->program() == "paw")
            context.compositor = process;
    auto request = line + '\n';
    std::string reply;
    if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) ==
        static_cast<ssize_t>(request.size())) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char buffer[4096];
        for (;;) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
            pollfd readable{fd, POLLIN, 0};
            if (left <= 0 || poll(&readable, 1, static_cast<int>(left)) <= 0)
                break;
            ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count <= 0) {
                context.replied = !reply.empty();
                break;
            }
            reply.append(buffer, static_cast<size_t>(count));
        }
    }
    close(fd);
}

Context gather() {
    Context context;
    context.processes = user_processes();
    context.socket = variable("PAW_SOCKET");
    if (!context.socket.empty())
        ask_compositor(context, "get workspace");
    if (!context.compositor) {
        for (const auto &process : context.processes)
            if (process.program() == "paw" && process.running() && process.has_argument("--session") &&
                (!context.compositor || process.stat.start > context.compositor->stat.start))
                context.compositor = process;
    }
    if (context.compositor) {
        auto &argv = context.compositor->argv;
        context.login = context.compositor->has_argument("--session") ||
                        context.compositor->environment().count("PAW_LOGIN_SESSION");
        context.version =
            paw_version("/proc/" + std::to_string(context.compositor->pid) + "/exe");
        // The configuration it was started with: --config PATH, relative to where it started.
        auto option = std::find(argv.begin(), argv.end(), "--config");
        if (option != argv.end() && option + 1 != argv.end()) {
            context.config_path = *(option + 1);
            if (context.config_path.is_relative()) {
                std::error_code error;
                auto directory = fs::read_symlink(
                    "/proc/" + std::to_string(context.compositor->pid) + "/cwd", error);
                if (!error)
                    context.config_path = directory / context.config_path;
            }
        }
    }
    // An `extends = "default"` reads the running compositor's default configuration: its own
    // PAW_DEFAULT_CONFIG, else the one installed beside its program, where this paw, built for
    // another prefix, would look elsewhere.
    if (context.compositor && variable("PAW_DEFAULT_CONFIG").empty()) {
        auto theirs = context.compositor->environment()["PAW_DEFAULT_CONFIG"];
        auto beside = context.prefix() / "share/paw/init.lua";
        std::error_code error;
        if (theirs.empty() && !context.prefix().empty() && fs::is_regular_file(beside, error))
            theirs = beside.string();
        if (!theirs.empty())
            setenv("PAW_DEFAULT_CONFIG", theirs.c_str(), 1);
    }
    if (context.config_path.empty()) {
        fs::path personal;
        if (auto xdg = variable("XDG_CONFIG_HOME"); !xdg.empty())
            personal = fs::path(xdg) / "paw/init.lua";
        else if (auto home = variable("HOME"); !home.empty())
            personal = fs::path(home) / ".config/paw/init.lua";
        context.config_path =
            !personal.empty() && fs::exists(personal) ? personal : default_config_path();
    }
    try {
        context.config = load_config(context.config_path);
    } catch (const std::exception &error) {
        context.config_error = error.what();
    }
    if (context.compositor && context.compositor->has_argument("--session"))
        if (auto state = state_directory(); !state.empty())
            context.log = doctor::since_last_start(read_file(state / "session.log"));
#if PAW_DOCTOR_BUS
    if (auto address = variable("DBUS_SESSION_BUS_ADDRESS"); !address.empty()) {
        auto bus = Bus::connect(address, context.bus_error);
        if (bus && bus->answers(context.bus_error))
            context.bus = std::move(bus);
    }
#endif
    return context;
}

// Session

Result check_session(const Context &context) {
    Result result{session_section, "session"};
    auto pid = context.compositor ? std::to_string(context.compositor->pid) : "";
    if (context.socket.empty()) {
        result.status = context.compositor ? Status::warning : Status::error;
        result.finding =
            context.compositor
                ? "PAW_SOCKET is unset: this shell is outside the paw session of PID " + pid
                : "Not in a paw session: PAW_SOCKET is unset, and no paw --session runs";
        result.fix = context.compositor ? "Run paw doctor in a terminal of that session"
                                        : "Log in to paw, and run paw doctor in a terminal there";
    } else if (!context.connected) {
        result.status = Status::error;
        result.finding = "Nothing answers on " + context.socket + " (" + context.socket_error +
                         "): the compositor has exited";
        result.fix = "Log out and back in";
    } else if (!context.replied) {
        result.status = Status::error;
        result.finding = "paw" + (pid.empty() ? "" : " (PID " + pid + ")") +
                         " does not answer on " + context.socket + ": it hangs";
        result.fix = "From a text console (Ctrl+Alt+F2): kill -TERM " +
                     (pid.empty() ? std::string("PID") : pid) +
                     " ends the session; then log in again";
    } else {
        result.finding = "paw " + (context.version.empty() ? "" : context.version + " ") +
                         "answers" + (pid.empty() ? "" : " (PID " + pid + ")");
        if (auto own = paw_version("/proc/self/exe"); !own.empty() && own != context.version &&
                                                       !context.version.empty())
            result.finding += "; this paw is " + own;
    }
    return result;
}

std::optional<Result> check_shell(const Context &context) {
    if (!context.compositor)
        return std::nullopt;
    Result result{session_section, "shell"};
    if (context.config && !context.config->shell.enabled) {
        result.finding = "The desktop shell is off (shell.enabled = false)";
        return result;
    }
    if (context.compositor->has_argument("--no-shell")) {
        result.finding = "The desktop shell is off (paw started with --no-shell)";
        return result;
    }
    for (const auto &process : context.processes)
        if (process.stat.ppid == context.compositor->pid && process.program() == "paw-shell" &&
            process.running()) {
            result.finding = "paw-shell runs (PID " + std::to_string(process.pid) + ")";
            return result;
        }
    result.status = Status::error;
    result.finding = "The desktop shell is not running, so the screen shows only the background "
                     "colour";
    if (auto why = context.logged("Cannot start desktop shell: "); !why.empty())
        result.finding += " (it cannot start: " + why + ")";
    result.finding += "; reloading paw starts it again";
    result.fix = context.reload();
    return result;
}

std::optional<Result> check_build(const Context &context) {
    if (!context.compositor || context.compositor->exe.empty())
        return std::nullopt;
    Result result{session_section, "build"};
    const auto &process = *context.compositor;
    auto path = doctor::program_path(process.exe);
    auto pid = std::to_string(process.pid);
    auto commit = doctor::version_commit(context.version);
    struct stat info;
    bool exists = stat(path.c_str(), &info) == 0;
    // A build installed since replaces the file, which leaves the compositor's " (deleted)"; one
    // written over it in place is newer than the process. A second of slack for the rounding.
    bool replaced = doctor::program_replaced(process.exe) ||
                    (exists && info.st_mtime > process.started() + 1);
    if (!replaced) {
        result.finding = "paw (PID " + pid + ") runs " + path +
                         (commit.empty() ? "" : " at " + commit) + ", as installed";
        return result;
    }
    auto installed = exists ? paw_version(path) : "";
    result.status = Status::warning;
    result.finding = "paw (PID " + pid + ") runs an older build than " + path + ", from " +
                     local_time(process.started());
    if (!context.version.empty() && !installed.empty())
        result.finding += ": " + context.version + " runs, " + installed + " is installed";
    result.fix = "Log out and back in to start the new build";
    return result;
}

// Whether update-paw is running for this state directory now.
bool updating(const Context &context, const fs::path &state) {
    for (const auto &process : context.processes) {
        bool script = std::any_of(process.argv.begin(), process.argv.end(), [](const auto &word) {
            return fs::path(word).filename() == "update-paw";
        });
        if (!script || !process.running())
            continue;
        auto env = process.environment();
        fs::path directory;
        if (auto xdg = env["XDG_STATE_HOME"]; !xdg.empty() && xdg[0] == '/')
            directory = fs::path(xdg) / "paw";
        else if (auto home = env["HOME"]; !home.empty())
            directory = fs::path(home) / ".local/state/paw";
        std::error_code error;
        if (!directory.empty() && fs::equivalent(directory, state, error))
            return true;
    }
    return false;
}

// The log of the update script that builds and installs paw before a session (update-paw), when
// there is one.
std::optional<Result> check_update(const Context &context) {
    auto state = state_directory();
    auto path = state / "update.log";
    std::error_code error;
    if (state.empty() || !fs::is_regular_file(path, error))
        return std::nullopt;
    auto log = doctor::parse_update_log(read_file(path));
    if (log.building.empty())
        return std::nullopt;
    // Its log says which commit it installed only after a build; it keeps the last one installed
    // in PREFIX/.installed-commit too.
    if (log.installed.empty() && !context.prefix().empty()) {
        std::istringstream stamp(read_file(context.prefix() / ".installed-commit"));
        stamp >> log.installed;
    }
    Result result{session_section, "update"};
    if (log.installed == log.building) {
        result.finding = "update-paw installed " + short_commit(log.installed) + " at " +
                         log.installed_time;
        return result;
    }
    if (updating(context, state)) {
        result.finding = "update-paw is building " + short_commit(log.building) + " (since " +
                         log.building_time + ")";
        return result;
    }
    result.status = Status::warning;
    result.finding = "update-paw could not build or install " + short_commit(log.building) +
                     " (" + log.building_time + "), so paw stays at " +
                     (log.installed.empty() ? "the build installed before"
                                            : short_commit(log.installed)) +
                     "; the end of its log says why";
    result.fix = "tail -n 30 " + doctor::shell_word(path.string());
    if (!find_program("update-paw").empty())
        result.fix += "\nupdate-paw";
    return result;
}

Result check_config(const Context &context) {
    Result result{session_section, "config"};
    auto path = context.config_path.string();
    if (!context.config) {
        result.status = Status::error;
        result.finding = context.config_error;
        result.fix = "Fix that, then check it with: paw --check-config --config " +
                     doctor::shell_word(path);
        return result;
    }
    result.finding = path + " is valid (" + std::to_string(context.config->bindings.size()) +
                     " bindings)";
    return result;
}

std::optional<Result> check_profile(const Context &context) {
    auto saved = saved_profile();
    if (!context.config || saved.empty())
        return std::nullopt;
    Result result{session_section, "profile"};
    const auto &profiles = context.config->profiles;
    if (std::find(profiles.begin(), profiles.end(), saved) != profiles.end()) {
        result.finding = "The saved profile " + saved + " is in the configuration";
        return result;
    }
    result.status = Status::warning;
    auto file = doctor::shell_word(profile_state_path().string());
    if (profiles.empty()) {
        result.finding = "The saved profile " + saved + " (" + profile_state_path().string() +
                         ") is gone: the configuration has no profiles";
        result.fix = "rm " + file;
    } else {
        auto active = context.config->profile.empty() ? profiles.front() : context.config->profile;
        result.finding = "The saved profile " + saved + " is not in the configuration, so paw "
                         "starts with " +
                         (context.config->profile.empty() ? "none" : context.config->profile) +
                         "; picking a profile saves it";
        result.fix = "paw msg profile " + doctor::shell_word(active);
    }
    return result;
}

// D-Bus

#if PAW_DOCTOR_BUS
// The program that owns `name` on `bus`, as "mako (PID 123)"; its process's program in `program`.
std::string owner_of(Bus &bus, const Context &context, const std::string &name,
                     std::string &program, int &pid) {
    auto owner = bus.owner(name);
    if (owner.empty())
        return {};
    pid = bus.pid(owner);
    auto *process = context.process(pid);
    program = process ? process->program() : "";
    // Another user's process (logind's is root's) shows only its name.
    if (program.empty() && pid > 0) {
        program = read_file("/proc/" + std::to_string(pid) + "/comm");
        if (!program.empty() && program.back() == '\n')
            program.pop_back();
    }
    if (program.empty())
        return owner + (pid ? " (PID " + std::to_string(pid) + ")" : "");
    return program + " (PID " + std::to_string(pid) + ")";
}
#endif

Result check_bus(const Context &context) {
    Result result{bus_section, "bus"};
    auto address = variable("DBUS_SESSION_BUS_ADDRESS");
    if (address.empty()) {
        result.status = Status::error;
        result.finding = "DBUS_SESSION_BUS_ADDRESS is unset: no notifications, tray, portals or "
                         "sound settings";
        result.fix = "Start the session with paw-session, which starts a session bus where "
                     "there is none (dbus-run-session paw --session)";
        return result;
    }
#if PAW_DOCTOR_BUS
    if (context.bus) {
        result.finding = "The session bus answers (" + address + ")";
        return result;
    }
    result.status = Status::error;
    result.finding = "The session bus at " + address + " does not answer (" + context.bus_error +
                     "): notifications, the tray, portals and sound settings stop working";
    result.fix = "Log out and back in: the session lost its bus";
#else
    (void)context;
    result.status = Status::warning;
    result.finding = "D-Bus was not checked: this paw was built without sd-bus";
    result.fix = "Rebuild paw with libsystemd, libelogind or basu installed";
#endif
    return result;
}

std::optional<Result> check_notifications(const Context &context) {
#if PAW_DOCTOR_BUS
    if (!context.bus)
        return std::nullopt;
    Result result{bus_section, "notifications"};
    bool ours = !context.config || context.config->notifications.enabled;
    std::string program;
    int pid = 0;
    auto owner = owner_of(*context.bus, context, "org.freedesktop.Notifications", program, pid);
    if (owner.empty()) {
        result.status = Status::warning;
        if (!ours) {
            result.finding = "Nothing shows notifications: paw's are off (notifications.enabled "
                             "= false) and no other notification daemon runs";
            result.fix = "Set notifications.enabled = true, or start a notification daemon";
        } else {
            auto why = context.logged("paw notifications: ");
            result.finding = "Nothing shows notifications" +
                             (why.empty() ? std::string(": paw's shell does not serve them")
                                          : ": paw's shell says " + why) +
                             "; reloading paw starts them again";
            result.fix = context.reload();
        }
    } else if (program == "paw-shell") {
        result.finding = "paw's shell shows notifications (PID " + std::to_string(pid) + ")";
    } else if (!ours) {
        result.finding = owner + " shows notifications (paw's are off)";
    } else {
        result.status = Status::warning;
        result.finding = owner + " shows notifications instead of paw's shell (to keep it, set "
                                 "notifications.enabled = false)";
        result.fix = (pid ? "kill " + std::to_string(pid) + " && " : std::string()) +
                     context.reload() + "\nTake " + (program.empty() ? "it" : program) +
                     " out of your autostart and startup commands";
    }
    return result;
#else
    (void)context;
    return std::nullopt;
#endif
}

std::optional<Result> check_polkit(const Context &context) {
    if (!context.compositor)
        return std::nullopt;
    Result result{bus_section, "polkit"};
    if (!context.login) {
        result.finding = "Password prompts are left to the host's polkit agent (a nested session)";
        return result;
    }
    std::vector<const Process *> agents;
    for (const auto &process : context.processes)
        if (process.running() && process.program() != "paw-shell" &&
            process.program() != "polkit-agent-helper-1" &&
            autostart_role(process.program(), {doctor::program_path(process.exe)}) == "polkit")
            agents.push_back(&process);
    std::string names;
    for (const auto *agent : agents)
        names += (names.empty() ? "" : ", ") + agent->program() + " (PID " +
                 std::to_string(agent->pid) + ")";
    if (context.config && !context.config->shell.polkit_agent) {
        result.finding = agents.empty() ? "paw's polkit agent is off (shell.polkit_agent = false)"
                                        : names + " asks for passwords (paw's agent is off)";
        return result;
    }
    auto said = context.logged("paw polkit: ");
    if (agents.empty() && (said.empty() || said == "the session's authentication agent")) {
        result.finding = "paw's shell asks for passwords (polkit)";
        return result;
    }
    result.status = Status::warning;
    if (!agents.empty()) {
        result.finding = names + " runs beside paw's shell, and only the polkit agent that "
                                 "came first asks for passwords (to keep it, set "
                                 "shell.polkit_agent = false)";
        std::string pids;
        for (const auto *agent : agents)
            pids += " " + std::to_string(agent->pid);
        result.fix = "kill" + pids + " && " + context.reload() +
                     "\nTake it out of your autostart and startup commands";
    } else {
        result.finding = "paw's shell is not the polkit agent: " + said +
                         "; reloading paw has it try again";
        result.fix = context.reload();
    }
    return result;
}

std::optional<Result> check_logind(const Context &context) {
#if PAW_DOCTOR_BUS
    Result result{bus_section, "logind"};
    // As the compositor finds it: the bus PAW_LOGIN1_BUS names, else the system bus.
    auto address = variable("PAW_LOGIN1_BUS");
    bool system = address.empty();
    std::string error;
    auto bus = Bus::connect(system ? "system" : address, error);
    if (bus && !bus->answers(error))
        bus.reset();
    bool openrc = fs::exists("/run/openrc");
    if (!bus) {
        result.status = Status::error;
        result.finding = std::string(system ? "The system bus" : "The bus at " + address) +
                         " does not answer (" + error + "): no suspend, reboot or power off from paw";
        result.fix = openrc ? "sudo rc-service dbus start" : "sudo systemctl start dbus";
        return result;
    }
    std::string program;
    int pid = 0;
    auto owner = owner_of(*bus, context, "org.freedesktop.login1", program, pid);
    if (owner.empty()) {
        result.status = Status::error;
        result.finding = "logind is not running: suspend, reboot and power off from paw fail";
        result.fix = openrc ? "sudo rc-service elogind start" : "sudo systemctl start systemd-logind";
        return result;
    }
    auto can = bus->string_call("org.freedesktop.login1", "/org/freedesktop/login1",
                                "org.freedesktop.login1.Manager", "CanSuspend", error);
    result.finding = owner + " answers" + (can ? " (suspend: " + *can + ")" : "");
    return result;
#else
    (void)context;
    return std::nullopt;
#endif
}

} // namespace

int doctor(int argc, char **argv) {
    bool plain = false, color = isatty(STDOUT_FILENO) && variable("NO_COLOR").empty() &&
                                variable("TERM") != "dumb";
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plain")
            plain = true;
        else if (arg == "--no-color")
            color = false;
        else
            throw std::runtime_error("usage: paw doctor [--plain] [--no-color]");
    }
    auto context = gather();
    std::vector<Result> results;
    auto add = [&](std::optional<Result> result) {
        if (result)
            results.push_back(std::move(*result));
    };
    add(check_session(context));
    add(check_shell(context));
    add(check_build(context));
    add(check_update(context));
    add(check_config(context));
    add(check_profile(context));
    add(check_bus(context));
    add(check_notifications(context));
    add(check_polkit(context));
    add(check_logind(context));
    std::cout << (plain ? doctor::plain_report(results) : doctor::report(results, color));
    return std::any_of(results.begin(), results.end(),
                       [](const Result &result) { return result.status == Status::error; })
               ? 1
               : 0;
}

} // namespace paw::cli
