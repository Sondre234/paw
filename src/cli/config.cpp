// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw config [edit]`: where the configuration paw starts with lives, and the personal one
 * opened in the user's editor, made first when there is none and checked once it is saved. */
#include "paw/cli.hpp"
#include "paw/config.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace paw::cli {

std::filesystem::path personal_configuration_path() {
    if (const auto *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "paw/init.lua";
    if (const auto *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config/paw/init.lua";
    return {};
}

std::filesystem::path configuration_path() {
    auto personal = personal_configuration_path();
    std::error_code failure;
    if (!personal.empty() && std::filesystem::exists(personal, failure))
        return personal;
    return default_config_path();
}

std::string starter_configuration() {
    return "-- Your paw configuration. It extends the default one, so it only needs what you\n"
           "-- change, and saving it reloads the session. The default configuration, with a\n"
           "-- comment on each setting and binding, is\n"
           "--   " +
           default_config_path().string() +
           "\n"
           "return {\n"
           "    version = 1,\n"
           "    extends = \"default\",\n"
           "}\n";
}

namespace {

std::string contents(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

// $VISUAL, else $EDITOR, a shell command the file's path is added to as git does; else
// xdg-open, the desktop's application for the file.
std::string editor() {
    for (const char *name : {"VISUAL", "EDITOR"})
        if (const char *value = std::getenv(name); value && *value)
            return value;
    return "xdg-open";
}

// Runs `command` on `path` and waits for it, leaving Ctrl+C and Ctrl+\ to the editor
// meanwhile; its exit status (127 when the shell cannot find it), or -1 when it cannot start.
int run_editor(const std::string &command, const std::filesystem::path &path) {
    struct sigaction ignore{}, interrupt{}, quit{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGINT, &ignore, &interrupt);
    sigaction(SIGQUIT, &ignore, &quit);
    pid_t pid = fork();
    if (pid == 0) {
        sigaction(SIGINT, &interrupt, nullptr);
        sigaction(SIGQUIT, &quit, nullptr);
        std::string script = command + " \"$@\"";
        execl("/bin/sh", "sh", "-c", script.c_str(), "sh", path.c_str(),
              static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    pid_t waited = pid;
    while (pid > 0 && (waited = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
    }
    sigaction(SIGINT, &interrupt, nullptr);
    sigaction(SIGQUIT, &quit, nullptr);
    if (pid < 0 || waited < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

} // namespace

int config(int argc, char **argv) {
    if (argc > 3 || (argc == 3 && argv[2] != std::string_view("edit")))
        throw std::runtime_error("usage: paw config [edit]");
    auto personal = personal_configuration_path();
    if (argc == 2) {
        auto path = configuration_path();
        std::cout << path.string() << '\n';
        if (!personal.empty() && path != personal)
            std::cerr << "There is no " << personal.string()
                      << " yet, so paw starts with the default configuration; paw config edit "
                         "makes one.\n";
        return 0;
    }
    if (personal.empty())
        throw std::runtime_error("cannot tell where the configuration lives: neither "
                                 "$XDG_CONFIG_HOME nor $HOME is set");
    std::error_code failure;
    bool made = !std::filesystem::exists(personal, failure);
    if (made) {
        std::filesystem::create_directories(personal.parent_path());
        std::ofstream file(personal, std::ios::binary);
        file << starter_configuration();
        if (!file.flush())
            throw std::runtime_error("cannot write " + personal.string());
    }
    auto before = contents(personal);
    auto command = editor();
    int status = run_editor(command, personal);
    if (made)
        std::cerr << "Made " << personal.string()
                  << ", which extends the default configuration; paw reads it from its next "
                     "start.\n";
    if (status == 127)
        throw std::runtime_error("cannot run the editor " + command +
                                 "; set $VISUAL or $EDITOR to the one you use");
    if (status != 0)
        throw std::runtime_error("the editor " + command + " exited with status " +
                                 std::to_string(status));
    if (contents(personal) != before)
        (void)load_config(personal);
    return 0;
}

} // namespace paw::cli
