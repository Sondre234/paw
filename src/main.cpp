// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/autostart.hpp"
#include "paw/config.hpp"
#include "paw/import.hpp"
#include "version.h"
#include <wlr/version.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <signal.h>
#include <span>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

extern char **environ;
namespace {
bool has_env(const char *name) {
    const char *value = std::getenv(name);
    return value && *value;
}
/* Starts `command`, with `extra_env` laid over the environment and in `directory` when one is
 * given: its pid, or -1 with `failure` saying why. */
pid_t start_program(const paw::Command &command, std::string &failure,
                    const std::vector<std::string> &extra_env = {},
                    const std::string &directory = {}) {
    std::vector<char *> argv;
    for (const auto &arg : command)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    std::vector<char *> env;
    auto overridden = [&](const char *entry) {
        for (const auto &extra : extra_env)
            if (std::strncmp(entry, extra.c_str(), extra.find('=') + 1) == 0)
                return true;
        return false;
    };
    for (char **entry = environ; entry && *entry; ++entry)
        if (!overridden(*entry))
            env.push_back(*entry);
    for (const auto &entry : extra_env)
        env.push_back(const_cast<char *>(entry.c_str()));
    env.push_back(nullptr);
    // Wayland's signal event sources block signals in the compositor. Children
    // need an ordinary signal mask so their own shutdown handling still works.
    posix_spawnattr_t attributes;
    int error = posix_spawnattr_init(&attributes);
    if (error) {
        failure = std::string("cannot prepare a child process: ") + std::strerror(error);
        return -1;
    }
    sigset_t mask;
    sigemptyset(&mask);
    error = posix_spawnattr_setsigmask(&attributes, &mask);
    if (!error)
        error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    posix_spawn_file_actions_t actions;
    bool in_directory = false;
    if (!error && !directory.empty()) {
        error = posix_spawn_file_actions_init(&actions);
        in_directory = !error;
        if (in_directory)
            error = posix_spawn_file_actions_addchdir_np(&actions, directory.c_str());
    }
    if (!error)
        error = posix_spawnp(&pid, argv[0], in_directory ? &actions : nullptr, &attributes,
                             argv.data(), env.data());
    if (in_directory)
        posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error)
        failure = "cannot launch " + command.front() +
                  (directory.empty() ? "" : " in " + directory) + ": " + std::strerror(error);
    return error ? -1 : pid;
}
/* start_program, saying on standard error when it fails. */
pid_t spawn(const paw::Command &command, const std::vector<std::string> &extra_env = {}) {
    std::string failure;
    pid_t pid = start_program(command, failure, extra_env);
    if (pid < 0)
        std::cerr << static_cast<char>(std::toupper(static_cast<unsigned char>(failure[0])))
                  << failure.substr(1) << '\n';
    return pid;
}
/* D-Bus-activated services such as xdg-desktop-portal start with the bus's environment, not
 * ours, so screen sharing and file choosers need to learn about this session. Only a standalone
 * session may do this: a nested one would point the host's portals at itself. */
void export_activation_environment() {
    // --systemd tells a systemd user manager too; where there is none it is quietly ignored.
    paw::Command command{"dbus-update-activation-environment", "--systemd"};
    for (const char *name :
         {"WAYLAND_DISPLAY", "DISPLAY", "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "PAW_SOCKET",
          "XCURSOR_THEME", "XCURSOR_SIZE"})
        if (has_env(name))
            command.emplace_back(name);
    pid_t pid = spawn(command);
    // Wait briefly, so a portal started by the first applications already sees this session.
    for (int tries = 0; pid > 0 && tries < 100; ++tries) {
        int status = 0;
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid && !(WIFEXITED(status) && WEXITSTATUS(status) == 0))
            std::cerr << "dbus-update-activation-environment failed: without a D-Bus session bus, "
                         "notifications, the tray and portals are missing; paw-session "
                         "starts one\n";
        if (done != 0)
            return;
        usleep(20000);
    }
    if (pid > 0)
        std::cerr << "dbus-update-activation-environment is still running; not waiting\n";
}
/* The dconf profile our children would read without us: $DCONF_PROFILE or "user", looked up
 * the way dconf does. Without a profile file, dconf reads just the user database. */
std::string dconf_profile() {
    const char *name = std::getenv("DCONF_PROFILE");
    std::string profile = name && *name ? name : "user";
    std::vector<std::filesystem::path> candidates;
    if (profile.starts_with('/')) {
        candidates.emplace_back(profile);
    } else {
        candidates.push_back(std::filesystem::path("/etc/dconf/profile") / profile);
        const char *data = std::getenv("XDG_DATA_DIRS");
        std::istringstream directories(data && *data ? data : "/usr/local/share:/usr/share");
        for (std::string directory; std::getline(directories, directory, ':');)
            if (!directory.empty())
                candidates.push_back(std::filesystem::path(directory) / "dconf/profile" / profile);
    }
    for (const auto &candidate : candidates) {
        std::ifstream file(candidate);
        if (!file)
            continue;
        std::string text{std::istreambuf_iterator<char>(file), {}};
        if (!text.empty() && text.back() != '\n')
            text += '\n';
        return text;
    }
    return "user-db:user\n";
}
/* GTK draws the buttons of client-decorated windows, Firefox's tab strip among them, from
 * org.gnome.desktop.wm.preferences button-layout. Desktops without title bar buttons (HyDE on
 * Hyprland) set it empty, which leaves such windows with no buttons at all. Our children get a
 * dconf profile that adds a database locking that one key to `layout`; every other setting
 * still reads from and writes to the user's own database, and other sessions see no change. */
void set_window_buttons(const std::string &layout) {
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    const char *display = std::getenv("WAYLAND_DISPLAY");
    if (layout.empty() || !runtime || *runtime != '/' || !display || !*display)
        return;
    auto directory = std::filesystem::path(runtime) / ("paw." + std::string(display) + ".dconf");
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory / "keyfiles/locks", error);
    std::ofstream(directory / "keyfiles/paw")
        << "[org/gnome/desktop/wm/preferences]\nbutton-layout='" << layout << "'\n";
    std::ofstream(directory / "keyfiles/locks/paw")
        << "/org/gnome/desktop/wm/preferences/button-layout\n";
    auto database = directory / "buttons";
    pid_t pid = spawn({"dconf", "compile", database.string(), (directory / "keyfiles").string()});
    int status = 0;
    if (pid <= 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        std::cerr << "Cannot set GTK window buttons: dconf compile failed\n";
        return;
    }
    auto profile = directory / "profile";
    std::ofstream(profile) << dconf_profile() << "file-db:" << database.string() << '\n';
    setenv("DCONF_PROFILE", profile.c_str(), true);
}
// The terminals the `terminal` action looks for, in this order, when neither the configuration
// nor $TERMINAL names one.
constexpr const char *known_terminals[] = {"kitty",   "foot",    "alacritty",      "wezterm",
                                           "ghostty", "konsole", "gnome-terminal", "xterm"};
std::filesystem::path home_directory() {
    const char *home = std::getenv("HOME");
    return home && *home ? home : "/";
}
/* $XDG_PICTURES_DIR from the environment or user-dirs.dirs, else ~/Pictures. */
std::filesystem::path pictures_directory() {
    if (const char *pictures = std::getenv("XDG_PICTURES_DIR"); pictures && *pictures == '/')
        return pictures;
    const char *config = std::getenv("XDG_CONFIG_HOME");
    std::ifstream dirs(
        (config && *config ? std::filesystem::path(config) : home_directory() / ".config") /
        "user-dirs.dirs");
    for (std::string line; std::getline(dirs, line);) {
        if (!line.starts_with("XDG_PICTURES_DIR=\"") || !line.ends_with('"'))
            continue;
        auto value = line.substr(18, line.size() - 19);
        if (value.starts_with("$HOME/"))
            return home_directory() / value.substr(6);
        if (value.starts_with('/'))
            return value;
    }
    return home_directory() / "Pictures";
}
/* Runs slurp (for a region), grim, wl-copy, and notify-send one after another without blocking
 * the compositor. Arguments: file, mode, grim target (geometry or output name), copy, notify. */
constexpr const char *screenshot_script = R"sh(
file=$1 mode=$2 target=$3 copy=$4 notify=$5
if [ "$mode" = region ]; then
    target=$(slurp) || { echo "Screenshot cancelled" >&2; exit 0; }
fi
if [ "$mode" = output ]; then
    grim -o "$target" "$file"
else
    grim -g "$target" "$file"
fi || { echo "Screenshot failed: grim exited with status $?" >&2; exit 1; }
echo "Screenshot saved: $file" >&2
if [ "$copy" = 1 ]; then
    wl-copy --type image/png < "$file" || echo "Screenshot not copied: wl-copy failed" >&2
fi
if [ "$notify" = 1 ]; then
    notify-send -a paw -i "$file" "Screenshot saved" "$file"
fi
)sh";
struct Runtime {
    std::filesystem::path path;
    paw::Config config;
    paw::Command extra_command;
    bool allow_shell = false;
    bool standalone = false;
    pid_t shell_pid = -1;
    // The running screenshot script; another request is refused until it exits.
    pid_t screenshot_pid = -1;
    std::string target{}; // the output target of the action last resolved
    unsigned flags = 0;   // the sh_binding_flag bits of the binding last resolved
    std::string keys{};   // what binding_keys returned last
    int mode = 0;         // the binding mode `key` looks in: 0 outside any, else modes[mode - 1]
    paw::Command program{}; // the program of the spawn action last resolved
    bool watch = false; // whether saving a configuration file reloads (off in headless tests)
    int watch_fd = -1;
    // Why power.lock_command cannot lock ("" when it can), worked out once per load.
    std::optional<std::string> locker_problem{};
    // A login session runs XDG autostart: a standalone one, or a test's with
    // PAW_LOGIN_SESSION=1.
    bool login = false;
    std::string autostart_report{}; // what `get autostart` prints
    // The names of what startup, autostart and --exec started, to know their windows by.
    std::set<std::string> started_names{};
    void note_started(const paw::Command &command) {
        for (auto &name : paw::program_names(command))
            started_names.insert(std::move(name));
    }

    ~Runtime() {
        if (watch_fd >= 0)
            close(watch_fd);
    }

    void start_shell() {
#if PAW_HAS_SHELL
        if (!allow_shell || !config.shell.enabled || shell_pid > 0)
            return;
        try {
            auto binary =
                std::filesystem::canonical("/proc/self/exe").parent_path() / "paw-shell";
            // Software rendering never uses GLX, but libGLX loads the GPU vendor's whole GLX
            // driver when the process starts: with NVIDIA that is about 16 MB of memory the shell
            // does not need. A vendor name that matches nothing keeps it out; the shell puts the
            // original back (PAW_GLX_VENDOR) so the applications it launches see no change.
            std::vector<std::string> extra_env;
            if (config.shell.software_renderer && !std::getenv("QT_QUICK_BACKEND") &&
                !std::getenv("QSG_RHI_BACKEND")) {
                extra_env.push_back("__GLX_VENDOR_LIBRARY_NAME=paw-none");
                if (const char *vendor = std::getenv("__GLX_VENDOR_LIBRARY_NAME"))
                    extra_env.push_back(std::string("PAW_GLX_VENDOR=") + vendor);
            }
            // The shell of a login session speaks for it to polkit; a nested one leaves that to
            // the host's agent.
            if (login)
                extra_env.push_back("PAW_POLKIT_AGENT=1");
            shell_pid = spawn(
                {binary.string(), "-platform", "wayland", "--config", path.string()}, extra_env);
        } catch (const std::exception &error) {
            std::cerr << "Cannot start desktop shell: " << error.what() << '\n';
        }
#endif
    }
    static void child_exited(void *data, int pid) {
        auto &self = *static_cast<Runtime *>(data);
        if (pid == self.shell_pid) {
            self.shell_pid = -1;
            std::cerr << "Desktop shell exited; reload the configuration to restart it\n";
        }
        if (pid == self.screenshot_pid)
            self.screenshot_pid = -1;
    }

    static const sh_settings *settings(void *data) {
        return &static_cast<Runtime *>(data)->config.settings;
    }
    static float opacity(void *data, const char *app_id, const char *title, bool active) {
        return static_cast<Runtime *>(data)->config.window_opacity(app_id, title, active);
    }
    static bool window_rule(void *data, const char *app_id, const char *title,
                            sh_window_rule *rule) {
        auto actions = static_cast<Runtime *>(data)->config.window_actions(app_id, title);
        if (actions.empty())
            return false;
        *rule = actions.to_c();
        return true;
    }
    static void dynamic_rule(void *data, const char *app_id, const char *title,
                             sh_dynamic_rule *rule) {
        auto held = static_cast<Runtime *>(data)->config.dynamic_actions(app_id, title);
        auto value = [](const std::optional<bool> &decided) { return decided ? *decided : -1; };
        *rule = {value(held.floating), value(held.sticky), value(held.above)};
    }
    /* The action of a key, button or switch binding, or SH_NONE without one; its argument goes
     * in `argument`, and its program, target and flags are kept for the callbacks after. */
    sh_action take(const paw::Binding *binding, int *argument) {
        if (!binding)
            return SH_NONE;
        if (binding->action == SH_SPAWN)
            program = binding->command;
        *argument = binding->action == SH_SCREENSHOT ? binding->screenshot : binding->workspace;
        if (paw::action_takes_amount(binding->action))
            *argument = binding->amount;
        if (binding->action == SH_SWITCH_LAYOUT)
            *argument = binding->layout;
        if (binding->action == SH_MODE || binding->action == SH_DISPLAY_MODE)
            *argument = binding->mode;
        target = binding->output;
        flags = (binding->locked ? SH_BINDING_LOCKED : 0) |
                (binding->repeats ? SH_BINDING_REPEATS : 0);
        return binding->action;
    }
    static sh_action key(void *data, uint32_t modifiers, uint32_t keysym, int *argument) {
        auto &self = *static_cast<Runtime *>(data);
        return self.take(self.config.mode_binding(self.mode, modifiers, keysym), argument);
    }
    static unsigned binding_flags(void *data) { return static_cast<Runtime *>(data)->flags; }
    static const char *set_mode(void *data, int mode) {
        auto &self = *static_cast<Runtime *>(data);
        if (mode < 0 || static_cast<std::size_t>(mode) > self.config.modes.size())
            return nullptr;
        self.mode = mode;
        return mode ? self.config.modes[static_cast<std::size_t>(mode) - 1].name.c_str()
                    : "default";
    }
    static sh_action button(void *data, uint32_t modifiers, uint32_t button,
                            sh_pointer_target target, const char *app_id, int *argument) {
        auto &self = *static_cast<Runtime *>(data);
        return self.take(self.config.button_binding(modifiers, button, target, app_id), argument);
    }
    /* The same for a switch turning on or off: { switch = "lid", state = "close", ... }. */
    static sh_action switch_toggled(void *data, sh_switch type, bool on, int *argument) {
        auto &self = *static_cast<Runtime *>(data);
        return self.take(self.config.switch_binding(type, on), argument);
    }
    /* The keys bound to an action, for the compositor to tell the user. */
    static const char *binding_keys(void *data, sh_action action) {
        auto &self = *static_cast<Runtime *>(data);
        self.keys = self.config.binding_keys(action);
        return self.keys.c_str();
    }
    /* Control requests: "<action> [workspace]", "screenshot [region|output|window]",
     * "resize_<direction> [pixels]", "volume_up|volume_down|brightness_up|brightness_down
     * [percent]", "switcher_confirm [N]", "switch_layout [next|prev|N]", "mode NAME|default",
     * "display_off [OUTPUT]" (and on, toggle), "display_mode [extend|duplicate|internal|
     * external]", "profile NAME|next|prev", or "spawn PROGRAM [ARGS...]". */
    static sh_action command(void *data, const char *request, int *argument, char *error,
                             size_t error_size) {
        auto &self = *static_cast<Runtime *>(data);
        self.target.clear();
        std::istringstream stream(request);
        std::vector<std::string> words{std::istream_iterator<std::string>(stream), {}};
        try {
            if (words.empty())
                throw std::runtime_error("empty request");
            if (words[0] == "profile")
                return self.pick_profile(words);
            sh_action action = paw::parse_action(words[0]);
            if (action == SH_SPAWN) {
                if (words.size() < 2)
                    throw std::runtime_error("spawn needs a program");
                self.program = {words.begin() + 1, words.end()};
                return action;
            }
            // The words after the action's name, for an argument that may hold spaces.
            auto rest = paw::join(std::span(words).subspan(1), " ");
            if (paw::action_takes_workspace(action)) {
                // A number, or a name from layout.workspace_names.
                int number = self.config.workspace_number(rest);
                if (!number)
                    throw std::runtime_error(words[0] + " needs a workspace from 1 to " +
                                             std::to_string(self.config.settings.workspaces) +
                                             ", or a workspace name");
                *argument = number;
            } else if (paw::action_takes_output(action)) {
                if (words.size() == 1 && action == SH_SWAP_WORKSPACES)
                    rest = "next";
                if (!paw::valid_output_target(rest))
                    throw std::runtime_error(words[0] + " takes one output: left, right, next, "
                                                        "prev, or a connector name");
                self.target = rest;
            } else if (paw::action_takes_display(action)) {
                // One monitor, or every one without a name.
                if (!rest.empty() && !paw::valid_output_target(rest))
                    throw std::runtime_error(words[0] + " takes a monitor's connector name, or "
                                                        "none for every monitor");
                self.target = rest;
            } else if (action == SH_DISPLAY_MODE) {
                if (words.size() > 2)
                    throw std::runtime_error("display_mode takes one mode: extend, duplicate, "
                                             "internal or external; none for the popup");
                *argument = words.size() == 2 ? paw::parse_display_mode(words[1])
                                              : SH_DISPLAY_MODE_STEP;
            } else if (action == SH_SCREENSHOT) {
                if (words.size() > 2)
                    throw std::runtime_error(
                        "screenshot takes one mode: region, output, or window");
                *argument = words.size() == 2 ? paw::parse_screenshot_mode(words[1])
                                              : SH_SCREENSHOT_REGION;
            } else if (paw::action_takes_amount(action)) {
                std::size_t used = 0;
                int amount = paw::default_amount(action);
                try {
                    if (words.size() == 2)
                        amount = std::stoi(words[1], &used);
                } catch (const std::logic_error &) {
                    amount = 0;
                }
                int most = paw::max_amount(action);
                if (words.size() > 2 || (words.size() == 2 && used != words[1].size()) ||
                    amount < 1 || amount > most)
                    throw std::runtime_error(
                        words[0] +
                        (most == 100 ? " takes a step in percent, from 1 to "
                                     : " takes a size in pixels, from 1 to ") +
                        std::to_string(most));
                *argument = amount;
            } else if (action == SH_MODE) {
                int number = words.size() == 2 ? self.config.mode_number(words[1]) : -1;
                if (number < 0) {
                    auto names = paw::join(self.config.mode_names());
                    throw std::runtime_error(words.size() == 2 ? "no mode " + words[1] +
                                                                     "; the modes are " + names
                                                               : "mode takes a mode's name: " +
                                                                     names);
                }
                *argument = number;
            } else if (action == SH_SWITCH_LAYOUT) {
                try {
                    if (words.size() > 2)
                        throw std::runtime_error("too many words");
                    *argument = words.size() == 2 ? paw::parse_layout_choice(words[1]) : 0;
                } catch (const std::runtime_error &) {
                    throw std::runtime_error("switch_layout takes next, prev, or a layout's "
                                             "number from 1");
                }
            } else if ((action == SH_SWITCHER_CONFIRM || action == SH_OVERVIEW_CONFIRM) &&
                       words.size() == 2) {
                std::size_t used = 0;
                int number = 0;
                try {
                    number = std::stoi(words[1], &used);
                } catch (const std::logic_error &) {
                }
                if (used != words[1].size() || number < 1)
                    throw std::runtime_error(
                        words[0] + " takes a window's place in the list, from 1");
                *argument = number;
            } else if (words.size() != 1) {
                throw std::runtime_error(words[0] + " takes no argument");
            }
            return action;
        } catch (const std::exception &failure) {
            std::snprintf(error, error_size, "%s", failure.what());
            return SH_NONE;
        }
    }
    /* "profile NAME", or "profile next|prev" for the neighbouring one in name order, wrapping:
     * saves the choice and reloads, so the compositor and the shell both take it up. */
    sh_action pick_profile(const std::vector<std::string> &words) {
        const auto &names = config.profiles;
        if (words.size() != 2)
            throw std::runtime_error("profile takes one profile name, next, or prev");
        if (names.empty())
            throw std::runtime_error("the configuration has no profiles");
        auto name = words[1];
        if (name == "next" || name == "prev") {
            auto at = std::find(names.begin(), names.end(), config.profile);
            auto index = static_cast<std::size_t>(at - names.begin());
            if (at == names.end())
                index = name == "next" ? 0 : names.size() - 1;
            else
                index = (index + (name == "next" ? 1 : names.size() - 1)) % names.size();
            name = names[index];
        } else if (std::find(names.begin(), names.end(), name) == names.end()) {
            throw std::runtime_error("no profile " + name + "; the profiles are " +
                                     paw::join(names));
        }
        paw::save_profile(name);
        return SH_RELOAD;
    }
    static const char *action_target(void *data) {
        return static_cast<Runtime *>(data)->target.c_str();
    }
    /* The terminal the `terminal` action opens: `terminal` from the configuration, else
     * $TERMINAL when that is installed, else the first of known_terminals that is. Empty when
     * there is none. */
    paw::Command terminal() const {
        if (!config.terminal.empty())
            return config.terminal;
        if (const char *name = std::getenv("TERMINAL"); name && *name) {
            if (paw::executable(name))
                return {name};
            std::cerr << "$TERMINAL, " << name << ", is not installed; looking for another\n";
        }
        for (const char *name : known_terminals)
            if (paw::executable(name))
                return {name};
        return {};
    }
    /* Starts the program of the spawn action that key, button, command or hot_corner returned
     * last, or the terminal. */
    static bool launch(void *data, sh_action action, char *error, size_t error_size) {
        auto &self = *static_cast<Runtime *>(data);
        auto program = action == SH_TERMINAL ? self.terminal() : self.program;
        std::string failure = "nothing to launch";
        if (program.empty() && action == SH_TERMINAL) {
            failure = "no terminal installed: set terminal in the configuration, or install one "
                      "of";
            for (const char *name : known_terminals)
                failure += std::string(name == known_terminals[0] ? " " : ", ") + name;
        }
        if (!program.empty() && start_program(program, failure) > 0)
            return true;
        std::snprintf(error, error_size, "%s", failure.c_str());
        return false;
    }
    static sh_action hot_corner(void *data, int corner, int *argument) {
        auto &self = *static_cast<Runtime *>(data);
        if (corner < 0 || corner >= 4 || self.config.hot_corners[corner].empty())
            return SH_NONE;
        char error[256] = "";
        auto action = command(data, self.config.hot_corners[corner].c_str(), argument, error,
                              sizeof(error));
        if (action == SH_NONE)
            std::cerr << "paw: hot corner: " << error << '\n';
        return action;
    }
    std::filesystem::path screenshot_directory() const {
        const auto &directory = config.screenshots.directory;
        if (directory.starts_with("~/"))
            return home_directory() / directory.substr(2);
        if (!directory.empty())
            return directory;
        return pictures_directory() / "Screenshots";
    }
    static bool screenshot(void *data, sh_screenshot_mode mode, const char *output,
                           const sh_rect *box, char *error, size_t error_size) {
        auto &self = *static_cast<Runtime *>(data);
        try {
            if (self.screenshot_pid > 0)
                throw std::runtime_error("a screenshot is already being taken");
            if (!paw::executable("grim") ||
                (mode == SH_SCREENSHOT_REGION && !paw::executable("slurp")))
                throw std::runtime_error(mode == SH_SCREENSHOT_REGION
                                             ? "region screenshots need grim and slurp installed"
                                             : "screenshots need grim installed");
            bool copy = self.config.screenshots.clipboard;
            if (copy && !paw::executable("wl-copy")) {
                std::cerr << "wl-copy is not installed; the screenshot is saved but not copied\n";
                copy = false;
            }
            bool notify = self.config.screenshots.notify && paw::executable("notify-send");
            auto directory = self.screenshot_directory();
            std::error_code failure;
            std::filesystem::create_directories(directory, failure);
            if (failure)
                throw std::runtime_error("cannot create " + directory.string() + ": " +
                                         failure.message());
            char stamp[64];
            std::time_t now = std::time(nullptr);
            std::strftime(stamp, sizeof(stamp), "Screenshot_%Y-%m-%d_%H-%M-%S",
                          std::localtime(&now));
            auto file = directory / (std::string(stamp) + ".png");
            for (int i = 2; std::filesystem::exists(file); ++i)
                file = directory / (std::string(stamp) + "-" + std::to_string(i) + ".png");
            std::string target;
            if (mode == SH_SCREENSHOT_OUTPUT)
                target = output;
            else if (mode == SH_SCREENSHOT_WINDOW)
                target = std::to_string(box->x) + "," + std::to_string(box->y) + " " +
                         std::to_string(box->width) + "x" + std::to_string(box->height);
            static constexpr const char *modes[] = {"region", "output", "window"};
            self.screenshot_pid =
                spawn({"/bin/sh", "-c", screenshot_script, "paw-screenshot", file.string(),
                       modes[mode], target, copy ? "1" : "0", notify ? "1" : "0"});
            if (self.screenshot_pid < 0)
                throw std::runtime_error("cannot start /bin/sh");
            return true;
        } catch (const std::exception &failure) {
            std::snprintf(error, error_size, "%s", failure.what());
            return false;
        }
    }
    /* power.lock_command: whether it can lock, and with `start` starting it. */
    static bool lock(void *data, bool start, char *error, size_t error_size) {
        auto &self = *static_cast<Runtime *>(data);
        const auto &command = self.config.power.lock_command;
        if (!self.locker_problem) {
            if (command.empty())
                self.locker_problem = "power.lock_command is not set";
            else if (!paw::executable(command.front()))
                self.locker_problem = command.front() + " is not installed";
            else
                self.locker_problem = "";
        }
        if (self.locker_problem->empty() && start && spawn(command) < 0)
            std::snprintf(error, error_size, "cannot start %s", command.front().c_str());
        else if (!self.locker_problem->empty())
            std::snprintf(error, error_size, "no screen locker: %s", self.locker_problem->c_str());
        else
            return true;
        return false;
    }
    static bool reload(void *data) {
        auto &self = *static_cast<Runtime *>(data);
        try {
            std::string error;
            auto next = paw::load_config_or_default(self.path, error);
            self.config = std::move(next);
            self.mode = 0; // the modes may have changed
            self.locker_problem.reset();
            // The shell loads the file too, and shows the error.
            if (self.shell_pid > 0)
                kill(self.shell_pid, SIGHUP);
            else
                self.start_shell();
            if (!error.empty())
                report_error(error);
            std::cerr << "Configuration reloaded: " << self.path;
            if (!self.config.profile.empty())
                std::cerr << " (profile " << self.config.profile << ')';
            std::cerr << '\n';
            return true;
        } catch (const std::exception &error) {
            std::cerr << "Reload rejected; keeping active configuration: " << error.what() << '\n';
            return false;
        } catch (...) {
            std::cerr << "Reload rejected; keeping active configuration: unexpected error\n";
            return false;
        }
    }
    /* Watches the configuration's directory (and, when the file is a link, its target's), as
     * editors often save by writing a new file and renaming it over the old one. */
    static int config_watch(void *data) {
        auto &self = *static_cast<Runtime *>(data);
        if (!self.watch)
            return -1;
        self.watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (self.watch_fd < 0) {
            std::cerr << "Cannot watch the configuration: " << std::strerror(errno) << '\n';
            return -1;
        }
        std::vector<std::filesystem::path> directories{self.path.parent_path()};
        std::error_code failure;
        auto target = std::filesystem::canonical(self.path, failure);
        if (!failure && target.parent_path() != directories.front())
            directories.push_back(target.parent_path());
        for (const auto &directory : directories)
            if (inotify_add_watch(self.watch_fd, directory.c_str(),
                                  IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE) < 0)
                std::cerr << "Cannot watch " << directory.string() << ": "
                          << std::strerror(errno) << '\n';
        return self.watch_fd;
    }
    static bool config_changed(void *data) {
        auto &self = *static_cast<Runtime *>(data);
        bool changed = false;
        // XKB keymaps count as well: an .xkb file, even while a broken one has the default
        // configuration standing in, or keyboard.file by any name.
        const std::filesystem::path file = self.config.settings.keyboard_file;
        const auto keymap = file.parent_path() == self.path.parent_path().lexically_normal()
                                ? file.filename()
                                : std::filesystem::path();
        alignas(inotify_event) char buffer[4096];
        for (ssize_t count; (count = read(self.watch_fd, buffer, sizeof(buffer))) > 0;) {
            for (char *at = buffer; at < buffer + count;) {
                auto *event = reinterpret_cast<inotify_event *>(at);
                std::string_view name = event->len ? event->name : "";
                if (name.ends_with(".lua") || name.ends_with(".xkb") ||
                    (!keymap.empty() && name == keymap.native()))
                    changed = true;
                at += sizeof(inotify_event) + event->len;
            }
        }
        return changed && self.config.auto_reload;
    }
    static void report_error(const std::string &error) {
        std::cerr << "Configuration error; using the default configuration: " << error << '\n';
    }
    /* XDG autostart: starts the entries of the autostart directories, noting for `get autostart`
     * what was started or skipped, and why. */
    void run_autostart() {
        autostart_report.clear();
        if (!login || !config.autostart.xdg)
            return;
        paw::AutostartOptions options{config.autostart.exclude, paw::current_desktops()};
        // What the shell provides itself, as it will once it runs.
        [[maybe_unused]] bool shell = false;
#if PAW_HAS_SHELL
        shell = allow_shell && config.shell.enabled;
#endif
#if PAW_SHELL_NOTIFICATIONS
        options.notifications = shell && config.notifications.enabled;
#endif
#if PAW_SHELL_TRAY
        options.tray = shell && config.shell.widgets.tray;
#endif
#if PAW_SHELL_POLKIT
        options.polkit = shell && config.shell.polkit_agent;
#endif
        options.sound = paw::sound_server_running();
        auto field = [](std::string text) {
            for (auto &c : text)
                c = c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
            return text;
        };
        int started = 0, skipped = 0;
        for (const auto &entry :
             paw::autostart_entries(paw::autostart_directories(), options)) {
            std::string state = "skipped", detail = entry.skip, failure;
            if (!entry.skip.empty()) {
                ++skipped;
            } else if (start_program(entry.command, failure, {}, entry.directory) > 0) {
                state = "started";
                note_started(entry.command);
                // Its windows may be called after its file, or what it says they are called.
                note_started({entry.name.substr(0, entry.name.size() - 8)});
                if (!entry.wm_class.empty())
                    note_started({entry.wm_class});
                detail = paw::join(entry.command, " ");
                ++started;
            } else {
                state = "failed";
                detail = failure;
                std::cerr << "Autostart " << entry.name << ": " << failure << '\n';
            }
            autostart_report += field(entry.name) + '\t' + state + '\t' + field(detail) + '\t' +
                                field(entry.path.string()) + '\n';
        }
        std::cerr << "XDG autostart: started " << started << ", skipped " << skipped
                  << " (paw msg get autostart lists them)\n";
    }
    static const char *autostart(void *data) {
        return static_cast<Runtime *>(data)->autostart_report.c_str();
    }
    static bool started(void *data, const char *app_id, const char *program) {
        return paw::started_by(static_cast<Runtime *>(data)->started_names,
                                    app_id ? app_id : "", program ? program : "");
    }
    static void startup(void *data) {
        auto &self = *static_cast<Runtime *>(data);
        if (self.standalone)
            export_activation_environment();
        set_window_buttons(self.config.window_buttons);
        self.start_shell();
        for (const auto &command : self.config.startup)
            if (spawn(command) > 0)
                self.note_started(command);
        self.run_autostart();
        if (!self.extra_command.empty() && spawn(self.extra_command) > 0)
            self.note_started(self.extra_command);
    }
};
std::filesystem::path personal_config() {
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "paw/init.lua";
    if (const auto *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config/paw/init.lua";
    return {};
}
std::filesystem::path default_config() {
    auto personal = personal_config();
    if (!personal.empty() && std::filesystem::exists(personal))
        return personal;
    if (auto shipped = paw::default_config_path(); std::filesystem::exists(shipped))
        return shipped;
    throw std::runtime_error(
        "no configuration found; use --config config/init.lua from the source directory");
}
/* `paw msg ...` sends one request to the running compositor's control socket. */
int send_message(int argc, char **argv) {
    std::string request;
    for (int i = 2; i < argc; ++i)
        request += (i > 2 ? " " : "") + std::string(argv[i]);
    if (request.empty() || request.find('\n') != std::string::npos)
        throw std::runtime_error("usage: paw msg [output NAME] ACTION [ARGUMENT] | "
                                 "get workspace|workspaces|tiling|windows|outputs|animations");
    const char *path = std::getenv("PAW_SOCKET");
    if (!path || !*path)
        throw std::runtime_error("PAW_SOCKET is not set; run inside a paw session");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(address.sun_path))
        throw std::runtime_error("control socket path is too long");
    std::strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error(std::string("cannot connect to ") + path + ": " +
                                 std::strerror(errno));
    }
    request += '\n';
    for (size_t sent = 0; sent < request.size();) {
        ssize_t written = write(fd, request.data() + sent, request.size() - sent);
        if (written <= 0) {
            close(fd);
            throw std::runtime_error("cannot send request");
        }
        sent += static_cast<size_t>(written);
    }
    std::string reply;
    char buffer[4096];
    for (ssize_t count; (count = read(fd, buffer, sizeof(buffer))) > 0;)
        reply.append(buffer, static_cast<size_t>(count));
    close(fd);
    bool ok = reply.rfind("ok", 0) == 0;
    auto body = reply.substr(std::min(reply.find('\n') + 1, reply.size()));
    if (ok)
        std::cout << body;
    else
        std::cerr << "paw: " << (reply.empty() ? "no reply\n" : reply);
    return ok ? 0 : 1;
}
/* `paw import [--config PATH] [--dry-run] DIR` writes theme.lua beside the configuration. */
int import_dotfiles(int argc, char **argv) {
    std::filesystem::path config, source;
    bool dry_run = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
            config = argv[++i];
        else if (arg == "--dry-run")
            dry_run = true;
        else if (source.empty() && !arg.starts_with("-"))
            source = arg;
        else
            throw std::runtime_error("usage: paw import [--config PATH] [--dry-run] DIR");
    }
    if (source.empty())
        throw std::runtime_error("usage: paw import [--config PATH] [--dry-run] DIR");
    if (config.empty())
        config = personal_config();
    if (config.empty())
        throw std::runtime_error("cannot tell where the configuration lives; pass --config PATH");
    auto result = paw::import_dotfiles(source);
    if (dry_run) {
        std::cout << result.theme;
        std::cerr << "Would import " << result.imported << " settings:\n" << result.report;
        return 0;
    }
    config = std::filesystem::absolute(config);
    auto target = config.parent_path() / "theme.lua";
    std::filesystem::create_directories(target.parent_path());
    auto temporary = target;
    temporary += ".new";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << result.theme;
        if (!file.flush())
            throw std::runtime_error("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, target);
    std::cout << "Wrote " << result.imported << " settings to " << target.string() << ":\n"
              << result.report;
    if (!std::filesystem::exists(config)) {
        std::cout << "\nThere is no " << config.string()
                  << " yet. Copy the default configuration there; it loads theme.lua.\n";
        return 0;
    }
    auto shadowed = paw::shadowed_settings(config);
    if (!shadowed)
        std::cout << "\nAdd  theme = \"theme.lua\",  to " << config.string() << " to use it.\n";
    else if (!shadowed->empty()) {
        std::cout << "\n"
                  << config.string()
                  << " sets these itself, so they override the import; delete them there to "
                     "use the imported values:\n";
        for (const auto &name : *shadowed)
            std::cout << "  " << name << '\n';
    }
    (void)paw::load_config(config);
    return 0;
}
void usage() {
    std::cout
        << "Usage: paw [--config PATH] [--check-config] [--headless | --session] "
           "[--exec PROGRAM [ARGS...]]\n"
           "Default: nested Wayland compositor. --session: standalone DRM/libinput on a TTY.\n"
           "Config: $XDG_CONFIG_HOME/paw/init.lua or ~/.config/paw/init.lua\n"
           "Falls back to the installed default; use --config config/init.lua in the source tree.\n"
           "--no-shell disables automatic shell startup; headless mode never starts it.\n"
           "--version prints the version, and the wlroots version paw was built with.\n"
           "SIGHUP reloads configuration; SIGINT/SIGTERM exits.\n"
           "paw msg [output NAME] ACTION [ARGUMENT] runs an action in the running session;\n"
           "paw msg get workspace|workspaces|tiling|windows|outputs|animations prints its state.\n"
           "paw import [--config PATH] [--dry-run] DIR writes theme.lua beside the\n"
           "configuration from the Hyprland, Waybar, wallbash, and pywal files in DIR.\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::filesystem::path path;
        bool check = false;
        bool no_shell = false;
        sh_backend_mode mode = SH_BACKEND_NESTED;
        paw::Command command;
        if (argc >= 2 && std::string(argv[1]) == "msg")
            return send_message(argc, argv);
        if (argc >= 2 && std::string(argv[1]) == "import")
            return import_dotfiles(argc, argv);
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            }
            if (arg == "--version") {
                std::cout << "paw " PAW_VERSION "\n"
                             "built with wlroots " WLR_VERSION_STR "\n";
                return 0;
            }
            if (arg == "--config" && i + 1 < argc)
                path = argv[++i];
            else if (arg == "--check-config")
                check = true;
            else if (arg == "--no-shell")
                no_shell = true;
            else if (arg == "--headless" || arg == "--session") {
                if (mode != SH_BACKEND_NESTED)
                    throw std::runtime_error("choose only one backend mode");
                mode = arg == "--headless" ? SH_BACKEND_HEADLESS : SH_BACKEND_SESSION;
            } else if (arg == "--exec" && i + 1 < argc) {
                while (++i < argc)
                    command.emplace_back(argv[i]);
            } else
                throw std::runtime_error("unknown or incomplete option: " + arg);
        }
        if (path.empty())
            path = default_config();
        // A configuration with an error still starts the session, with the default one, so
        // that it can be fixed from there; --check-config reports it instead.
        std::string error;
        Runtime runtime{std::filesystem::absolute(path),
                        check ? paw::load_config(path)
                              : paw::load_config_or_default(path, error),
                        std::move(command)};
        if (!error.empty())
            Runtime::report_error(error);
        runtime.allow_shell = !no_shell && mode != SH_BACKEND_HEADLESS;
        runtime.standalone = mode == SH_BACKEND_SESSION;
        runtime.login = runtime.standalone || has_env("PAW_LOGIN_SESSION");
        // Tests rewrite their configuration and reload it themselves.
        runtime.watch = mode != SH_BACKEND_HEADLESS || has_env("PAW_AUTO_RELOAD");
        if (check) {
            std::cout << "Configuration valid: " << path << " (" << runtime.config.bindings.size()
                      << " bindings)\n";
            return 0;
        }
        if (mode == SH_BACKEND_NESTED && !has_env("WAYLAND_DISPLAY"))
            throw std::runtime_error("a running Wayland session is required (or use --headless)");
        if (mode == SH_BACKEND_SESSION && (has_env("WAYLAND_DISPLAY") || has_env("DISPLAY")))
            throw std::runtime_error("start --session from a TTY or a display manager, outside an "
                                     "existing graphical session");
        const sh_callbacks callbacks{
            &runtime,           Runtime::settings, Runtime::key,          Runtime::button,
            Runtime::command,   Runtime::reload,   Runtime::startup,      Runtime::child_exited,
            Runtime::opacity,   Runtime::screenshot, Runtime::window_rule,
            Runtime::hot_corner, Runtime::action_target, Runtime::config_watch,
            Runtime::config_changed, Runtime::lock, Runtime::launch, Runtime::autostart,
            Runtime::started, Runtime::binding_flags, Runtime::set_mode, Runtime::switch_toggled,
            Runtime::binding_keys, Runtime::dynamic_rule};
        int result = sh_run(&callbacks, mode);
        if (runtime.shell_pid > 0)
            kill(runtime.shell_pid, SIGTERM);
        return result;
    } catch (const std::exception &error) {
        std::cerr << "paw: " << error.what() << '\n';
        return 1;
    }
}
