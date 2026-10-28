// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw showcase`: a screenshot to show the desktop off. It saves the arrangement of the windows as
 * a session, opens the terminal running `paw fetch`, waits for it to settle, takes the monitor
 * with grim, then closes the terminal again and restores the session; on a failure or Ctrl+C
 * too. */
#include "paw/showcase.hpp"
#include "paw/autostart.hpp"
#include "paw/cli.hpp"
#include "paw/config.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace paw::showcase {

namespace {

const char *const usage = "usage: paw showcase [--delay SECONDS] [--keep] [--output NAME] "
                          "[-o FILE] [--terminal COMMAND]";

} // namespace

Options parse(int argc, char **argv) {
    Options options;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        bool more = i + 1 < argc;
        if (arg == "--keep")
            options.keep = true;
        else if (arg == "--delay" && more) {
            char *end = nullptr;
            options.delay = std::strtod(argv[++i], &end);
            if (end == argv[i] || *end || !(options.delay >= 0 && options.delay <= 600))
                throw std::runtime_error("--delay takes seconds from 0 to 600");
        } else if (arg == "--output" && more)
            options.output = argv[++i];
        else if (arg == "-o" && more)
            options.file = argv[++i];
        else if (arg == "--terminal" && more) {
            std::istringstream words(argv[++i]);
            options.terminal.clear();
            for (std::string word; words >> word;)
                options.terminal.push_back(word);
            if (options.terminal.empty())
                throw std::runtime_error("--terminal takes a command");
        } else
            throw std::runtime_error(usage);
    }
    return options;
}

std::vector<std::string> terminal_command(const std::vector<std::string> &terminal,
                                          const std::vector<std::string> &program) {
    auto command = terminal;
    auto name = terminal.empty() ? "" : std::filesystem::path(terminal[0]).filename().string();
    if (name == "kitty" || name == "foot" || name == "footclient") {
        // These run what follows their options.
    } else if (name == "wezterm") {
        if (terminal.size() == 1)
            command.push_back("start");
        command.push_back("--");
    } else if (name == "gnome-terminal" || name == "kgx" || name == "ptyxis" ||
               name == "gnome-console")
        command.push_back("--");
    else
        command.push_back("-e");
    command.insert(command.end(), program.begin(), program.end());
    return command;
}

std::filesystem::path pictures_directory(const char *environment, const std::string &user_dirs,
                                         const std::filesystem::path &home) {
    if (environment && *environment == '/')
        return environment;
    std::istringstream lines(user_dirs);
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("XDG_PICTURES_DIR=\"") || !line.ends_with('"'))
            continue;
        auto value = line.substr(18, line.size() - 19);
        if (value.starts_with("$HOME/"))
            return home / value.substr(6);
        if (value.starts_with('/'))
            return value;
    }
    return home / "Pictures";
}

std::string file_name(const std::tm &time) {
    char name[64];
    std::strftime(name, sizeof(name), "paw-showcase-%Y-%m-%d-%H%M%S.png", &time);
    return name;
}

} // namespace paw::showcase

namespace paw::cli {
namespace {

// What the terminal runs, given paw, the pipe and how many windows there were: paw fetch under
// the title "paw fetch", once the terminal's window shows (a second at most) for it to count, the
// text cursor hidden for the picture, then a word from the pipe. "keep" hands the terminal to the
// user's shell; anything else, the pipe closing as paw showcase ends among them, closes it.
const char *const held_fetch = R"sh(printf '\033]2;paw fetch\007'
i=0
while [ "$i" -lt 20 ] && [ "$("$0" msg get windows | wc -l)" -le "$2" ]; do
    sleep 0.05
    i=$((i + 1))
done
"$0" fetch
printf '\033[?25l'
read -r word < "$1"
if [ "$word" = keep ]; then
    printf '\033[?25h'
    exec "${SHELL:-/bin/sh}"
fi
)sh";

// The terminals the terminal action looks for without one configured (main.cpp).
const char *const known_terminals[] = {"kitty",   "foot",    "alacritty",      "wezterm",
                                       "ghostty", "konsole", "gnome-terminal", "xterm"};

volatile std::sig_atomic_t stopped = 0;

void stop(int) { stopped = 1; }

struct Stopped : std::runtime_error {
    Stopped() : std::runtime_error("interrupted") {}
};

// Waits until `done`, checking every 50 milliseconds, for `seconds` at most; false when the time
// ran out. Throws Stopped once a signal asks paw showcase to stop.
bool wait_until(const std::function<bool()> &done, double seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    for (;;) {
        if (stopped)
            throw Stopped();
        if (done())
            return true;
        if (std::chrono::steady_clock::now() >= end)
            return false;
        usleep(50'000);
    }
}

void send(const std::string &line) {
    auto reply = request(line);
    if (!reply.ok) {
        auto why = reply.body;
        while (!why.empty() && why.back() == '\n')
            why.pop_back();
        throw std::runtime_error(line + ": " + why);
    }
}

size_t windows() { return query("windows").size(); }

// Where the windows are and which has the focus: `get windows` without the titles, which change
// by themselves, in an order focusing leaves alone.
std::vector<std::string> arrangement() {
    std::vector<std::string> lines;
    for (auto row : query("windows")) {
        if (row.size() > 9)
            row.erase(row.begin() + 9);
        std::string line;
        for (const auto &field : row)
            line += field + '\t';
        lines.push_back(std::move(line));
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

// The terminal as the terminal action opens it: the configured one, else $TERMINAL, else the
// first one installed.
std::vector<std::string> configured_terminal() {
    try {
        auto config = load_config(configuration_path());
        if (!config.terminal.empty())
            return config.terminal;
    } catch (const std::exception &) {
    }
    if (const char *name = std::getenv("TERMINAL"); name && *name && executable(name))
        return {name};
    for (const char *name : known_terminals)
        if (executable(name))
            return {name};
    return {};
}

// Starts `command` with the signals as a program expects them; in a session of its own, away
// from the Ctrl+C typed for paw showcase, with `detach`.
pid_t start(const std::vector<std::string> &command, bool detach) {
    pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error(std::string("cannot start ") + command[0] + ": " +
                                 std::strerror(errno));
    if (pid == 0) {
        for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGPIPE})
            std::signal(signal, SIG_DFL);
        if (detach) {
            setsid();
            int null = open("/dev/null", O_RDWR);
            if (null >= 0) {
                dup2(null, STDIN_FILENO);
                dup2(null, STDOUT_FILENO);
                dup2(null, STDERR_FILENO);
            }
        }
        std::vector<char *> argv;
        for (const auto &word : command)
            argv.push_back(const_cast<char *>(word.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}

// Everything paw showcase changes, undone as far as it got.
struct Showcase {
    showcase::Options options;
    std::string session = "showcase-" + std::to_string(getpid());
    bool saved = false;
    std::filesystem::path directory{}, pipe{}, partial{};
    int writer = -1;
    pid_t terminal = -1;
    size_t before = 0;
    std::vector<std::string> arranged{}; // as they were
    bool reached = false;                // the terminal ran paw fetch, and waits on the pipe
    bool opened = false, hidden = false;

    // Hands the terminal its word: "keep" leaves it open, "close" closes it.
    void tell(const char *word) {
        if (writer < 0)
            return;
        [[maybe_unused]] auto written = write(writer, word, std::strlen(word));
        close(writer);
        writer = -1;
    }

    void undo() {
        if (hidden) {
            try {
                send("cursor show");
            } catch (const std::exception &) {
            }
        }
        if (!partial.empty()) {
            std::error_code failure;
            std::filesystem::remove(partial, failure);
        }
        if (writer >= 0)
            tell("close\n");
        else if (!reached && terminal > 0 && waitpid(terminal, nullptr, WNOHANG) == 0)
            kill(-terminal, SIGTERM); // a terminal that never ran paw fetch
        // The pipe gone, a terminal that comes to it only now finds nothing to wait for.
        if (!directory.empty()) {
            std::error_code failure;
            std::filesystem::remove_all(directory, failure);
            directory.clear();
        }
        if (opened) {
            try {
                stopped = 0;
                wait_until([&] { return windows() <= before; }, 5);
            } catch (const std::exception &) {
            }
        }
        // Closing the terminal puts the other windows back as they were, once the tiles have
        // made room again and the focus has come back; where it did not the session restores
        // them. Only then, as a restore tiles the windows again one by one, which can change their
        // order.
        if (saved) {
            try {
                stopped = 0;
                if (!wait_until([&] { return arrangement() == arranged; }, 2))
                    send("session restore " + session);
            } catch (const std::exception &) {
            }
        }
        forget();
    }

    void forget() {
        if (saved) {
            try {
                send("session delete " + session);
            } catch (const std::exception &) {
            }
            saved = false;
        }
        if (!directory.empty()) {
            std::error_code failure;
            std::filesystem::remove_all(directory, failure);
        }
    }

    std::filesystem::path run(const std::vector<std::string> &command,
                              const std::filesystem::path &file) {
        const char *runtime = std::getenv("XDG_RUNTIME_DIR");
        std::string model =
            std::string(runtime && *runtime == '/' ? runtime : "/tmp") + "/paw-showcase-XXXXXX";
        if (!mkdtemp(model.data()))
            throw std::runtime_error("cannot make a directory for the terminal's pipe: " +
                                     std::string(std::strerror(errno)));
        directory = model;
        pipe = directory / "pipe";
        if (mkfifo(pipe.c_str(), 0600) < 0)
            throw std::runtime_error("cannot make the terminal's pipe: " +
                                     std::string(std::strerror(errno)));
        send("session save " + session);
        saved = true;
        arranged = arrangement();
        before = arranged.size();

        auto self = std::filesystem::canonical("/proc/self/exe").string();
        terminal =
            start(showcase::terminal_command(command, {"/bin/sh", "-c", held_fetch, self,
                                                       pipe.string(), std::to_string(before)}),
                  true);
        // The pipe opens once paw fetch has written its lines; a terminal that ends at once has
        // handed the window to one already running (kitty --single-instance), unless it failed.
        bool ran = wait_until(
            [&] {
                writer = open(pipe.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
                int status = 0;
                if (writer < 0 && terminal > 0 && waitpid(terminal, &status, WNOHANG) > 0) {
                    terminal = -1;
                    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                        throw std::runtime_error(command[0] + " failed to start");
                }
                return writer >= 0;
            },
            20);
        if (!ran)
            throw std::runtime_error(command[0] + " did not run paw fetch within 20 seconds");
        reached = true;
        if (!wait_until([&] { return windows() > before; }, 10))
            throw std::runtime_error(command[0] + " opened no window");
        opened = true;
        // The window's opening animation, and the tiles making room, then the delay asked for.
        wait_until([] { return query("animations").at(0).at(0) == "0"; }, 3);
        wait_until([] { return false; }, options.delay);

        auto output = options.output;
        if (output.empty())
            for (const auto &row : query("workspaces"))
                if (row.size() >= 3 && row[2] == "1")
                    output = row[0];
        if (output.empty())
            throw std::runtime_error("no monitor to take");
        partial = file;
        partial += ".part";
        send("cursor hide");
        hidden = true;
        pid_t grim = start({"grim", "-t", "png", "-o", output, partial.string()}, false);
        int status = 0;
        bool done = false;
        try {
            done = wait_until([&] { return waitpid(grim, &status, WNOHANG) == grim; }, 30);
        } catch (const Stopped &) {
            kill(grim, SIGTERM);
            waitpid(grim, nullptr, 0);
            throw;
        }
        if (!done) {
            kill(grim, SIGTERM);
            waitpid(grim, nullptr, 0);
            throw std::runtime_error("grim took no picture within 30 seconds");
        }
        if (hidden) {
            send("cursor show");
            hidden = false;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("grim could not take " + output);
        std::filesystem::rename(partial, file);
        partial.clear();

        if (options.keep) {
            tell("keep\n");
            forget();
        } else {
            tell("close\n");
            if (!wait_until([&] { return windows() <= before; }, 10))
                std::cerr << "paw: the terminal is still open\n";
            opened = false;
            undo();
        }
        return file;
    }
};

} // namespace

int showcase(int argc, char **argv) {
    auto options = showcase::parse(argc, argv);
    // Outside a session this says so.
    auto outputs = query("outputs");
    if (!options.output.empty() &&
        std::none_of(outputs.begin(), outputs.end(), [&](const auto &row) {
            return row.size() > 1 && row[0] == options.output && row[1] == "1";
        }))
        throw std::runtime_error("no monitor " + options.output + " in the layout");
    auto terminal = options.terminal.empty() ? configured_terminal() : options.terminal;
    if (terminal.empty())
        throw std::runtime_error("no terminal installed: set terminal in the configuration, or "
                                 "name one with --terminal");
    if (!executable(terminal[0]))
        throw std::runtime_error(terminal[0] + " is not installed");
    if (!executable("grim"))
        throw std::runtime_error("paw showcase takes its picture with grim, which is not "
                                 "installed");

    auto file = options.file;
    if (file.empty()) {
        const char *config = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        std::filesystem::path home_directory = home && *home ? home : "/";
        std::ifstream dirs(
            (config && *config ? std::filesystem::path(config) : home_directory / ".config") /
            "user-dirs.dirs");
        std::stringstream text;
        text << dirs.rdbuf();
        auto directory = showcase::pictures_directory(std::getenv("XDG_PICTURES_DIR"), text.str(),
                                                      home_directory);
        std::time_t now = std::time(nullptr);
        auto name = showcase::file_name(*std::localtime(&now));
        file = directory / name;
        for (int i = 2; std::filesystem::exists(file); ++i)
            file = directory / (name.substr(0, name.size() - 4) + "-" + std::to_string(i) + ".png");
    }
    file = std::filesystem::absolute(file);
    std::error_code failure;
    std::filesystem::create_directories(file.parent_path(), failure);
    if (failure)
        throw std::runtime_error("cannot create " + file.parent_path().string() + ": " +
                                 failure.message());

    // Ctrl+C, a hang-up or a TERM undoes what was done so far before paw showcase ends; a request
    // to the session it comes in the middle of goes on.
    struct sigaction action = {};
    action.sa_handler = stop;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGINT, SIGTERM, SIGHUP})
        sigaction(signal, &action, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    Showcase run{options};
    try {
        std::cout << run.run(terminal, file).string() << '\n';
        return 0;
    } catch (...) {
        // Whatever failed after a signal came, the signal ended it.
        bool interrupted = stopped;
        run.undo();
        if (!interrupted)
            throw;
        std::cerr << "paw: showcase interrupted; the windows are as they were\n";
        return 130;
    }
}

} // namespace paw::cli
