// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw tour`: a narrated demo of paw that drives the running session. It opens windows of its own
 * on an empty workspace of the focused monitor, takes the steps of its table (tour_steps.cpp) one
 * after the other, each with a caption the shell shows and the terminal prints, and then puts back
 * what it changed: it closes its windows, and only those, sets the monitor's tiling and its
 * workspace's layout, night light and the appearance profile back, and restores the arrangement it
 * saved as the session "tour" before it began, which takes the monitor back to its workspace. It
 * does that too when it is interrupted, when one of its windows is closed, and once the session
 * is unlocked when it was locked meanwhile. */
#include "paw/cli.hpp"
#include "paw/tour.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace paw::cli {
namespace {
using namespace paw::tour;
using Clock = std::chrono::steady_clock;

volatile std::sig_atomic_t signalled = 0;
void on_signal(int number) {
    signalled = number;
}

// Why the tour stops before its end; it then puts everything back.
struct Stop : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The scripts the tour's terminals run, by turns: `paw fetch` where there is one (else a card of
// its own), a clock in large figures, and colours drifting across the window. Each names its
// window by $PAW_TOUR_TITLE and runs paw as $PAW_TOUR_PAW.
constexpr const char *scripts[] = {
    R"sh(printf '\033]2;%s\007\033[?25l\033[H\033[2J' "$PAW_TOUR_TITLE"
if ! "$PAW_TOUR_PAW" fetch 2>/dev/null; then
    pink='\033[38;2;242;143;173m' blue='\033[38;2;137;180;250m' dim='\033[2m' off='\033[0m'
    version=$("$PAW_TOUR_PAW" --version 2>/dev/null | head -n 1)
    windows=$("$PAW_TOUR_PAW" msg get windows 2>/dev/null | wc -l)
    uptime=$(awk '{ s = int($1); printf "%dh %02dm", s / 3600, s % 3600 / 60 }' /proc/uptime 2>/dev/null)
    printf "\n$pink      ▄██▄   ▄██▄      $off  $blue%s$off\n" "${version:-paw}"
    printf "$pink      ▀██▀   ▀██▀      $off  ${dim}PAW Arranges Windows$off\n"
    printf "$pink  ▄██▄           ▄██▄  $off\n"
    printf "$pink  ▀██▀  ▄▄███▄▄  ▀██▀  $off  ${blue}kernel$off   %s\n" "$(uname -sr)"
    printf "$pink       █████████       $off  ${blue}uptime$off   %s\n" "${uptime:-?}"
    printf "$pink        ▀▀███▀▀        $off  ${blue}windows$off  %s\n\n" "$windows"
    printf '                         '
    for colour in 1 2 3 4 5 6 9 10 11 12 13 14; do printf '\033[48;5;%dm   ' $colour; done
    printf '\033[0m\n'
fi
while :; do sleep 3600; done)sh",
    R"sh(printf '\033]2;%s\007\033[?25l' "$PAW_TOUR_TITLE"
last=
while :; do
    size=$(stty size 2>/dev/null) || size='24 80'
    [ "$size" = "$last" ] || printf '\033[2J'
    last=$size
    date '+%H:%M:%S %A %e %B' | awk -v size="$size" '
    BEGIN {
        g["0"] = "111101101101111"; g["1"] = "010110010010111"; g["2"] = "111001111100111"
        g["3"] = "111001111001111"; g["4"] = "101101111001001"; g["5"] = "111100111001111"
        g["6"] = "111100111101111"; g["7"] = "111001001001001"; g["8"] = "111101111101111"
        g["9"] = "111101111001111"; g[":"] = "000010000010000"
        split(size, s, " "); rows = s[1]; cols = s[2]
    }
    {
        t = $1; d = $2 " " $3 " " $4
        if (cols < 50) t = substr(t, 1, 5)
        width = 0
        for (i = 1; i <= length(t); i++) width += substr(t, i, 1) == ":" ? 3 : 7
        top = int((rows - 7) / 2); left = int((cols - width) / 2)
        if (cols < width) { printf "\033[H\033[2K%s", t; exit }
        for (row = 0; row < 5; row++) {
            printf "\033[%d;%dH", top + row + 1, left + 1
            x = 0
            for (i = 1; i <= length(t); i++) {
                ch = substr(t, i, 1)
                for (c = 0; c < 3; c++) {
                    if (ch == ":" && c != 1) continue
                    f = x / width
                    if (substr(g[ch], row * 3 + c + 1, 1) == "1")
                        printf "\033[38;2;%d;%d;%dm██", 242 - 105 * f, 143 + 37 * f, 173 + 77 * f
                    else
                        printf "  "
                    x += 2
                }
                printf " "; x += 1
            }
        }
        printf "\033[0m\033[%d;%dH\033[2K\033[2m%s\033[0m", top + 7, int((cols - length(d)) / 2) + 1, d
    }'
    sleep 1
done)sh",
    R"sh(printf '\033]2;%s\007\033[?25l\033[2J' "$PAW_TOUR_TITLE"
awk 'BEGIN {
    for (frame = 0; ; frame++) {
        if (frame % 10 == 0) {
            rows = 24; cols = 80
            if (("stty size < /dev/tty 2>/dev/null" | getline line) > 0) {
                split(line, s, " "); rows = s[1]; cols = s[2]
            }
            close("stty size < /dev/tty 2>/dev/null")
        }
        t = frame * 0.12
        printf "\033[H"
        for (y = 0; y < rows; y++) {
            for (x = 0; x < int(cols / 2); x++) {
                v = sin(x * 0.16 + t) + sin(y * 0.33 - t * 1.3) + sin((x + y) * 0.11 + t * 0.7)
                h = (v + 3) / 6
                printf "\033[48;2;%d;%d;%dm  ", 150 + 95 * sin(6.283 * h), 150 + 80 * sin(6.283 * (h + 0.33)), 175 + 75 * sin(6.283 * (h + 0.66))
            }
            printf "\033[0m%s", y < rows - 1 ? "\n" : ""
        }
        fflush()
        system("sleep 0.2")
    }
}')sh",
};

// A line of `get windows`.
struct Window {
    int workspace = 0;
    bool focused = false;
    std::string app_id, title, output;
    bool visible = false;
};

std::vector<Window> list_windows() {
    std::vector<Window> windows;
    for (const auto &row : query("windows")) {
        if (row.size() < 12)
            continue;
        windows.push_back({std::atoi(row[0].c_str()), row[1] == "1", row[8], row[9], row[10],
                           row[11] == "1"});
    }
    return windows;
}

// A monitor as `get workspaces` has it.
struct Monitor {
    std::string name;
    int workspace = 0; // shown now, from 1
    bool focused = false;
    std::set<int> used; // the workspaces holding windows
    bool tiling = false;
};

std::vector<Monitor> list_monitors() {
    std::vector<Monitor> monitors;
    for (const auto &row : query("workspaces")) {
        if (row.size() < 5)
            continue;
        Monitor monitor{row[0], std::atoi(row[1].c_str()), row[2] == "1", {}, row[4] == "on"};
        std::istringstream used(row[3]);
        for (std::string number; std::getline(used, number, ',');)
            if (number != "-")
                monitor.used.insert(std::atoi(number.c_str()));
        monitors.push_back(std::move(monitor));
    }
    return monitors;
}

int milliseconds_since(Clock::time_point start) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    return text;
}

// What a command prints, its last newline left out; "" when it cannot run.
std::string output_of(const std::string &command) {
    std::string text;
    if (FILE *pipe = popen(command.c_str(), "r")) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe))
            text += buffer;
        pclose(pipe);
    }
    return trimmed(text);
}

// The control socket's state as a subscriber hears it, for whether the session is locked.
class Subscription {
  public:
    Subscription() {
        const char *path = std::getenv("PAW_SOCKET");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (!path || std::strlen(path) >= sizeof(address.sun_path))
            throw std::runtime_error("PAW_SOCKET is not set; run inside a paw session");
        std::strcpy(address.sun_path, path);
        fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd_ < 0 || connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 ||
            write(fd_, "subscribe\n", 10) != 10)
            throw std::runtime_error(std::string("cannot subscribe to ") + path);
        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
        // The state comes at once, ending with the power actions that may run.
        auto start = Clock::now();
        while (!power_ && milliseconds_since(start) < 5000) {
            pollfd ready{fd_, POLLIN, 0};
            poll(&ready, 1, 100);
            read();
        }
    }
    ~Subscription() {
        if (fd_ >= 0)
            close(fd_);
    }
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;
    int fd() const { return fd_; }
    bool locked() const { return locked_; }
    bool ended() const { return ended_; }
    // Takes in what has come.
    void read() {
        char buffer[4096];
        for (;;) {
            ssize_t count = ::read(fd_, buffer, sizeof(buffer));
            if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
                ended_ = true;
                return;
            }
            if (count < 0)
                return;
            pending_.append(buffer, static_cast<size_t>(count));
            for (size_t end; (end = pending_.find('\n')) != std::string::npos;) {
                auto line = pending_.substr(0, end);
                pending_.erase(0, end + 1);
                if (line == "locked on" || line == "locked off")
                    locked_ = line == "locked on";
                power_ = power_ || line.starts_with("power ");
            }
        }
    }

  private:
    int fd_ = -1;
    std::string pending_;
    bool locked_ = false, ended_ = false, power_ = false;
};

// A window the tour opened.
struct Demo {
    std::string title;
    pid_t pid = -1;
    bool shown = false;  // its window has been seen
    bool closing = false; // the tour closes it
};

class Tour {
  public:
    Tour(Options options, std::vector<Step> steps)
        : options_(std::move(options)), steps_(std::move(steps)) {}
    int run();
    // Prints the steps, with their keys and whether they run here when the session answers.
    int list();

  private:
    Options options_;
    std::vector<Step> steps_;
    std::optional<Subscription> subscription_;
    std::map<std::string, std::string> bindings_; // each action's keys, as `get actions`
    Offer offer_;
    std::map<std::string, std::string> values_; // what the steps' {NAME}s stand for
    std::string output_;       // the monitor the tour runs on
    int home_ = 0;             // its workspace before the tour
    int workspace_ = 0;        // the empty one the tour runs on
    std::string layout_;       // that workspace's layout before the tour
    bool tiling_ = false;      // whether it tiled
    std::string night_light_;  // the action that puts night light back
    std::string profile_;      // the appearance profile in use
    std::optional<std::string> profile_file_; // the saved profile's file as it was, if any
    std::string color_scheme_; // gsettings' color-scheme before the profile step, if read
    std::vector<std::string> terminal_;
    std::string caption_; // the caption shown last: TITLE<tab>BODY<tab>KEYS
    std::vector<Demo> demos_;
    bool saved_ = false, moved_ = false, restoring_ = false, stdin_open_ = true;
    Clock::time_point checked_{};

    void survey();
    void prepare();
    std::string night_light_action();
    void start();
    void take(size_t index, size_t count);
    void finish(bool stopped);
    Reply send(const std::string &line);
    void act(const std::string &request);
    void caption(const std::string &title, const std::string &body, const std::string &keys,
                 double seconds);
    void pause(int milliseconds);
    void wait_for_enter();
    void check();
    void wait_unlocked();
    bool ours(const std::string &title) const;
    bool focus_ours(bool other);
    void set_tiling(bool on);
    void open_windows(int count);
    pid_t spawn(const std::string &title, size_t number);
    void close_windows(bool one_by_one);
    void reap();
    std::optional<Monitor> monitor();
};

bool Tour::ours(const std::string &title) const {
    return std::any_of(demos_.begin(), demos_.end(),
                       [&](const Demo &demo) { return demo.title == title; });
}

std::optional<Monitor> Tour::monitor() {
    for (auto &monitor : list_monitors())
        if (monitor.name == output_)
            return monitor;
    return std::nullopt;
}

// A request to the session; while it is locked, refused requests wait for it to be unlocked as
// the tour puts things back, and stop the tour otherwise.
Reply Tour::send(const std::string &line) {
    for (;;) {
        auto reply = request(line);
        if (reply.ok || reply.body.find("the session is locked") == std::string::npos)
            return reply;
        if (!restoring_)
            throw Stop("the session was locked");
        wait_unlocked();
    }
}

void Tour::wait_unlocked() {
    std::cout << "The session is locked; putting things back once it is unlocked (Ctrl+C to "
                 "leave them).\n"
              << std::flush;
    signalled = 0;
    for (;;) {
        pollfd ready{subscription_->fd(), POLLIN, 0};
        poll(&ready, 1, 200);
        subscription_->read();
        if (!subscription_->locked())
            return;
        if (signalled || subscription_->ended())
            throw std::runtime_error("left as it was; `paw msg session restore tour` puts the "
                                     "windows back");
    }
}

// Stops the tour when it is interrupted, when the session locks or ends, and when one of its
// windows went away without the tour closing it.
void Tour::check() {
    subscription_->read();
    if (subscription_->ended())
        throw std::runtime_error("the session ended");
    if (restoring_)
        return;
    if (signalled)
        throw Stop(signalled == SIGINT ? "interrupted" : "asked to stop");
    if (subscription_->locked())
        throw Stop("the session was locked");
    if (milliseconds_since(checked_) < 200)
        return;
    checked_ = Clock::now();
    auto windows = list_windows();
    for (const auto &demo : demos_) {
        if (!demo.shown || demo.closing)
            continue;
        if (std::none_of(windows.begin(), windows.end(),
                         [&](const Window &window) { return window.title == demo.title; }))
            throw Stop("the window " + demo.title + " was closed");
    }
}

void Tour::pause(int milliseconds) {
    auto start = Clock::now();
    for (;;) {
        check();
        int left = milliseconds - milliseconds_since(start);
        if (left <= 0)
            return;
        pollfd ready{subscription_->fd(), POLLIN, 0};
        poll(&ready, 1, std::min(left, 100));
    }
}

void Tour::wait_for_enter() {
    if (!stdin_open_)
        return;
    std::cout << "        Enter for the next step\n" << std::flush;
    auto shown = Clock::now();
    for (;;) {
        check();
        pollfd ready[] = {{STDIN_FILENO, POLLIN, 0}, {subscription_->fd(), POLLIN, 0}};
        poll(ready, 2, 100);
        if (ready[0].revents & (POLLIN | POLLHUP)) {
            std::string line;
            if (!std::getline(std::cin, line))
                stdin_open_ = false; // at the end of the input, the tour goes on by itself
            return;
        }
        // The caption stays as long as the tour waits.
        if (milliseconds_since(shown) > 60000) {
            shown = Clock::now();
            request("caption 90 " + caption_);
        }
    }
}

void Tour::caption(const std::string &title, const std::string &body, const std::string &keys,
                   double seconds) {
    caption_ = title + '\t' + body + '\t' + keys;
    // Refused without the shell, when only the terminal says it.
    request("caption " + std::to_string(std::clamp(static_cast<int>(std::ceil(seconds)), 1, 600)) +
            ' ' + caption_);
}

void Tour::set_tiling(bool on) {
    auto shown = monitor();
    if (shown && shown->tiling != on)
        send("output " + output_ + " toggle_tiling");
}

// Gives one of the tour's windows the focus, another than the focused one with `other`; false
// when none would take it.
bool Tour::focus_ours(bool other) {
    std::string from;
    for (int attempt = 0; attempt < 12; ++attempt) {
        auto windows = list_windows();
        auto focused = std::find_if(windows.begin(), windows.end(),
                                    [](const Window &window) { return window.focused; });
        bool mine = focused != windows.end() && ours(focused->title) &&
                    focused->output == output_ && focused->visible;
        if (attempt == 0 && mine)
            from = focused->title;
        if (mine && (!other || focused->title != from))
            return true;
        // A tiling monitor steps through its own tiles; elsewhere every visible window takes a
        // turn, the least recently focused first.
        auto shown = monitor();
        send(mine && shown && shown->tiling ? "output " + output_ + " focus_next" : "cycle");
        check();
    }
    return false;
}

pid_t Tour::spawn(const std::string &title, size_t number) {
    std::vector<std::string> command;
    if (!options_.client.empty())
        command = {"/bin/sh", "-c", client_command(options_.client, title)};
    else
        command = terminal_command(terminal_, title, scripts[number % std::size(scripts)]);
    std::error_code error;
    auto paw = std::filesystem::read_symlink("/proc/self/exe", error);
    pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error("cannot start a window: " + std::string(std::strerror(errno)));
    if (pid == 0) {
        // In a session of its own, out of the terminal's reach: Ctrl+C there is the tour's alone.
        setsid();
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGHUP, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, STDIN_FILENO);
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
        }
        setenv("PAW_TOUR_TITLE", title.c_str(), 1);
        setenv("PAW_TOUR_PAW", error ? "paw" : paw.c_str(), 1);
        std::vector<char *> argv;
        for (auto &word : command)
            argv.push_back(word.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    return pid;
}

void Tour::open_windows(int count) {
    for (int i = 0; i < count; ++i) {
        auto title = "paw tour " + std::to_string(demos_.size() + 1);
        demos_.push_back({title, spawn(title, demos_.size())});
        auto started = Clock::now();
        std::optional<Window> found;
        while (!found) {
            for (auto &window : list_windows())
                if (window.title == title)
                    found = window;
            if (found)
                break;
            int status = 0;
            if (waitpid(demos_.back().pid, &status, WNOHANG) == demos_.back().pid) {
                demos_.back().pid = -1;
                throw Stop("the window " + title + " did not open: its program exited");
            }
            if (milliseconds_since(started) > 20000)
                throw Stop("the window " + title + " did not open in 20 seconds");
            pause(50);
        }
        demos_.back().shown = true;
        if (found->output != output_ || found->workspace != workspace_)
            throw Stop("the window " + title + " opened on " + found->output + ", workspace " +
                       std::to_string(found->workspace) + ", not the tour's; start it with the "
                       "pointer on the monitor it runs on");
        if (i + 1 < count)
            pause(scaled_ms(0.5, options_.speed));
    }
}

// Closes the tour's windows, the newest first: their programs are ended, and a window that
// outlives its program (a terminal that handed it to a copy running already) is closed as the
// user would.
void Tour::close_windows(bool one_by_one) {
    for (auto demo = demos_.rbegin(); demo != demos_.rend(); ++demo) {
        if (demo->closing)
            continue;
        demo->closing = true;
        if (demo->pid > 0 && kill(-demo->pid, SIGTERM) < 0)
            kill(demo->pid, SIGTERM);
        auto started = Clock::now();
        auto present = [&] {
            auto windows = list_windows();
            return std::any_of(windows.begin(), windows.end(),
                               [&](const Window &window) { return window.title == demo->title; });
        };
        bool killed = false;
        while (present() && milliseconds_since(started) < 4000) {
            if (!killed && milliseconds_since(started) > 2000 && demo->pid > 0) {
                killed = true;
                if (kill(-demo->pid, SIGKILL) < 0)
                    kill(demo->pid, SIGKILL);
            }
            pause(50);
        }
        if (present()) {
            for (int attempt = 0; attempt < 12 && present(); ++attempt) {
                auto windows = list_windows();
                auto focused = std::find_if(windows.begin(), windows.end(),
                                            [](const Window &window) { return window.focused; });
                if (focused != windows.end() && focused->title == demo->title)
                    send("close");
                else
                    send("cycle");
                pause(100);
            }
            if (present())
                std::cerr << "paw: could not close the window " << demo->title << '\n';
        }
        if (one_by_one)
            pause(scaled_ms(0.35, options_.speed));
    }
    reap();
}

void Tour::reap() {
    for (auto &demo : demos_) {
        if (demo.pid <= 0)
            continue;
        int status = 0;
        if (waitpid(demo.pid, &status, WNOHANG) == demo.pid)
            demo.pid = -1;
    }
}

void Tour::act(const std::string &raw) {
    auto request = expand(raw, values_);
    std::istringstream words(request);
    std::string first, second;
    words >> first >> second;
    if (first == "wait")
        return pause(scaled_ms(std::strtod(second.c_str(), nullptr), options_.speed));
    if (first == "open")
        return open_windows(std::atoi(second.c_str()));
    if (first == "tiling")
        return set_tiling(second == "on");
    if (request == "focus other") {
        focus_ours(true);
        return;
    }
    if (request == "close windows")
        return close_windows(true);
    if (acts_on_window(request) && !focus_ours(false)) {
        std::cerr << "        (" << request << " left out: none of the tour's windows has the "
                                                "focus)\n";
        return;
    }
    if (first == "profile" && color_scheme_.empty() && !output_of("command -v gsettings").empty())
        color_scheme_ = output_of(
            "gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null");
    auto reply = send(acts_on_output(request) ? "output " + output_ + " " + request : request);
    if (!reply.ok)
        std::cerr << "        (" << request << ": " << trimmed(reply.body) << ")\n";
}

// What the session offers the steps, and where the tour would run; changes nothing.
void Tour::survey() {
    subscription_.emplace();
    for (auto &monitor : list_monitors())
        if (monitor.focused) {
            output_ = monitor.name;
            home_ = monitor.workspace;
        }
    // Two workspaces of the focused monitor that hold no windows.
    auto shown = monitor();
    int spare = 0;
    for (int number = 1; shown && number <= 10; ++number) {
        // `get layout` answers only for the workspaces there are.
        if (!request("get layout " + output_ + ' ' + std::to_string(number)).ok)
            break;
        if (shown->used.contains(number))
            continue;
        if (!workspace_)
            workspace_ = number;
        else if (!spare)
            spare = number;
    }
    // An older compositor without these queries offers less.
    try {
        for (const auto &row : query("actions"))
            if (row.size() >= 2) {
                bindings_[row[0]] = row[1];
                offer_.actions.insert(row[0]);
            }
    } catch (const std::runtime_error &) {
    }
    std::vector<std::string> profiles;
    try {
        for (const auto &row : query("profiles")) {
            if (row.size() < 2)
                continue;
            profiles.push_back(row[0]);
            if (row[1] == "1")
                profile_ = row[0];
        }
    } catch (const std::runtime_error &) {
    }
    auto other = other_profile(profiles, profile_);
    offer_.other_profile = !other.empty();
    offer_.spare_workspace = spare != 0;
    offer_.scratchpad_free = query("scratchpad").empty();
    if (!other.empty()) {
        values_["profile"] = profile_;
        values_["other_profile"] = other;
    }
    if (spare)
        values_["spare"] = std::to_string(spare);
    night_light_ = night_light_action();
    values_["night_light"] = night_light_;
}

// The action that puts night light back as it is now: `get night_light`'s override, 0 the
// schedule, 1 neutral, 2 warm.
std::string Tour::night_light_action() {
    auto night = query("night_light");
    int mode = night.empty() || night[0].size() < 2 ? 0 : std::atoi(night[0][1].c_str());
    return mode == 1 ? "night_light_off" : mode == 2 ? "night_light_on" : "night_light_auto";
}

// Refuses to start where the tour would get in the way, and notes what it will put back.
void Tour::prepare() {
    survey();
    if (subscription_->locked())
        throw std::runtime_error("the session is locked; unlock it first");
    if (output_.empty())
        throw std::runtime_error("no monitor has the focus");
    if (!workspace_)
        throw std::runtime_error("every workspace of " + output_ + " holds windows; the tour "
                                 "needs an empty one");
    auto windows = list_windows();
    for (const auto &window : windows)
        if (window.title.starts_with("paw tour "))
            throw std::runtime_error("the window \"" + window.title + "\" of an earlier tour is "
                                     "still open; close it first");
    // A fullscreen window with the focus is a game or a video the user is in.
    auto focused = std::find_if(windows.begin(), windows.end(),
                                [](const Window &window) { return window.focused; });
    if (focused != windows.end()) {
        for (const auto &row : query("stacking")) {
            if (row.size() < 4 || row[0] != focused->app_id || row[1] != focused->title ||
                !row[3].starts_with("fullscreen"))
                continue;
            if (!isatty(STDIN_FILENO))
                throw std::runtime_error("a fullscreen window has the focus");
            std::cout << "A fullscreen window has the focus. Run the tour anyway? [y/N] "
                      << std::flush;
            std::string answer;
            if (!std::getline(std::cin, answer) || (answer != "y" && answer != "Y"))
                throw std::runtime_error("left alone");
            break;
        }
    }
    if (options_.client.empty()) {
        auto terminal = query("terminal");
        if (!terminal.empty())
            terminal_ = terminal.front();
        if (terminal_.empty() || terminal_.front().empty())
            throw std::runtime_error("there is no terminal to open; install one, or pass "
                                     "--client COMMAND");
    }
    if (offer_.other_profile) {
        auto path = state_directory() / "profile";
        if (std::ifstream file(path); !path.empty() && file)
            profile_file_ = std::string(std::istreambuf_iterator<char>(file), {});
    }
}

void Tour::start() {
    // The arrangement as it is, for putting back.
    auto saved = request("session save tour");
    if (!saved.ok)
        throw std::runtime_error("cannot save the arrangement first: " + trimmed(saved.body));
    saved_ = true;
    send("output " + output_ + " workspace " + std::to_string(workspace_));
    moved_ = true;
    if (auto shown = monitor())
        tiling_ = shown->tiling;
    auto layout = query("layout " + output_ + ' ' + std::to_string(workspace_));
    layout_ = layout.empty() ? "" : layout[0][0];
}

void Tour::take(size_t index, size_t count) {
    const auto &step = steps_[index];
    if (auto why = skip_reason(step, offer_); !why.empty()) {
        std::cout << '[' << index + 1 << '/' << count << "] " << step.title << " - skipped: " << why
                  << '\n'
                  << std::flush;
        return;
    }
    auto keys = caption_keys(step.keys, bindings_);
    std::cout << '[' << index + 1 << '/' << count << "] " << step.title
              << (keys.empty() ? "" : "  (" + keys + ")") << "\n        " << step.body << '\n'
              << std::flush;
    double seconds = scaled_ms(step_seconds(step), options_.speed) / 1000.0 + 3;
    caption(step.title, step.body, keys, options_.step ? 90 : seconds);
    if (demos_.empty()) {
        open_windows(3);
        caption(step.title, step.body, keys, options_.step ? 90 : seconds);
    }
    for (const auto &request : step.requests)
        act(request);
    if (options_.step)
        wait_for_enter();
    else
        pause(scaled_ms(step.hold, options_.speed));
}

void Tour::finish(bool stopped) {
    restoring_ = true;
    signalled = 0;
    try {
        if (stopped) {
            caption("Stopping the tour", "Its windows close, and your desktop goes back as it was.",
                    "", 10);
            send("switcher_cancel");
            auto overview = query("overview");
            if (!overview.empty() && !overview[0].empty() && overview[0][0].starts_with("open"))
                send("overview_cancel");
        }
        close_windows(false);
        if (!options_.restore) {
            request("caption hide");
            std::cout << "Left as the tour left it; `paw msg session restore tour` puts your "
                         "windows back.\n";
            return;
        }
        if (night_light_action() != night_light_)
            send(night_light_);
        if (values_.contains("other_profile")) {
            std::string now;
            for (const auto &row : query("profiles"))
                if (row.size() >= 2 && row[1] == "1")
                    now = row[0];
            if (now != profile_)
                send("profile " + profile_);
            // `profile` saved its choice; the file goes back to what it held, or away, once it
            // names the profile it had.
            auto path = state_directory() / "profile";
            std::ifstream file(path);
            std::string saved;
            if (file && std::getline(file, saved) && saved == profile_) {
                file.close();
                if (!profile_file_)
                    std::filesystem::remove(path);
                else if (*profile_file_ != saved + '\n')
                    std::ofstream(path, std::ios::trunc) << *profile_file_;
            }
            // A profile's appearance.color_scheme told applications to turn light or dark; a
            // profile without one leaves them so, unless told back.
            if (!color_scheme_.empty() &&
                output_of("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null") !=
                    color_scheme_)
                output_of("gsettings set org.gnome.desktop.interface color-scheme " +
                          shell_quote(color_scheme_));
        }
        if (moved_) {
            // The workspace's layout and the monitor's tiling are put back while it shows.
            if (auto shown = monitor(); shown && shown->workspace != workspace_)
                send("output " + output_ + " workspace " + std::to_string(workspace_));
            auto layout = query("layout " + output_ + ' ' + std::to_string(workspace_));
            if (!layout_.empty() && !layout.empty() && layout[0][0] != layout_)
                send("output " + output_ + " layout_" + layout_);
            set_tiling(tiling_);
        }
        if (saved_) {
            auto restored = send("session restore tour");
            if (!restored.ok)
                std::cerr << "paw: cannot restore the arrangement: " << trimmed(restored.body)
                          << '\n';
            if (auto shown = monitor(); shown && shown->workspace != home_)
                send("output " + output_ + " workspace " + std::to_string(home_));
            if (restored.ok)
                request("session delete tour");
        }
        request("caption hide");
        std::cout << "Your desktop is back as it was.\n";
    } catch (const std::exception &error) {
        std::cerr << "paw: " << error.what() << '\n';
        // The session gone or refusing, the tour's windows still go with it.
        for (auto &demo : demos_)
            if (demo.pid > 0 && kill(-demo.pid, SIGTERM) < 0)
                kill(demo.pid, SIGTERM);
    }
}

int Tour::run() {
    prepare();
    // Anew, as a question about a fullscreen window may have kept the first waiting long enough
    // for the compositor to drop it.
    subscription_.emplace();
    struct sigaction action {};
    action.sa_handler = on_signal; // no SA_RESTART: a wait wakes up at once
    sigemptyset(&action.sa_mask);
    for (int number : {SIGINT, SIGTERM, SIGHUP})
        sigaction(number, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
    size_t count = steps_.size();
    double seconds = 0;
    for (size_t i = options_.from - 1; i < count; ++i)
        seconds += skip_reason(steps_[i], offer_).empty() ? step_seconds(steps_[i]) : 0;
    std::cout << "paw tour: about " << std::lround(seconds / options_.speed + 5)
              << " seconds on " << output_ << ", workspace " << workspace_
              << ". Ctrl+C here, or closing one of its windows, stops it; either way your "
                 "desktop goes back as it was.\n"
              << std::flush;
    int status = 0;
    bool stopped = false;
    try {
        start();
        for (size_t i = options_.from - 1; i < count; ++i)
            take(i, count);
    } catch (const Stop &stop) {
        std::cout << "Stopping: " << stop.what() << ".\n" << std::flush;
        stopped = true;
        status = signalled == SIGINT ? 130 : 1;
    } catch (const std::exception &error) {
        std::cerr << "paw: " << error.what() << '\n';
        stopped = true;
        status = 1;
    }
    finish(stopped);
    return status;
}

int Tour::list() {
    // The keys, and whether each step runs here, when there is a session to ask.
    bool asked = false;
    try {
        survey();
        asked = true;
    } catch (const std::exception &) {
    }
    double seconds = 0;
    for (size_t i = 0; i < steps_.size(); ++i) {
        const auto &step = steps_[i];
        std::string keys;
        for (const auto &action : step.keys)
            keys += (keys.empty() ? "" : " / ") + action;
        if (asked)
            keys = caption_keys(step.keys, bindings_);
        auto why = asked ? skip_reason(step, offer_) : "";
        std::cout << (i < 9 ? " " : "") << i + 1 << "  " << step.title
                  << (keys.empty() ? "" : "  (" + keys + ")")
                  << (why.empty() ? "" : "  - skipped here: " + why) << "\n    " << step.body
                  << '\n';
        seconds += step_seconds(step);
    }
    std::cout << "About " << std::lround(seconds + 5) << " seconds at --speed 1.\n";
    return 0;
}

} // namespace

int tour(int argc, char **argv) {
    auto options = parse_options(argc, argv);
    auto steps = parse_steps(steps_table);
    if (options.from > static_cast<int>(steps.size()))
        throw std::runtime_error("--from takes a step's number, from 1 to " +
                                 std::to_string(steps.size()));
    Tour tour(options, std::move(steps));
    return options.list ? tour.list() : tour.run();
}

} // namespace paw::cli
