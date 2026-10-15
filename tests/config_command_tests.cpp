// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw config [edit]`: the path it prints, the personal configuration it makes, the editor it
 * runs ($VISUAL before $EDITOR, the path quoted) and the check once the file has changed.
 * Run with the example configuration as the default one. */
#include "check.h"
#include "paw/cli.hpp"
#include "paw/config.hpp"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Run {
    std::string out, err, error;
};

// `paw config ARGUMENTS...`, what it printed and the error it threw, if any.
Run run(std::vector<std::string> arguments) {
    arguments.insert(arguments.begin(), {"paw", "config"});
    std::vector<char *> argv;
    for (auto &argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::ostringstream out, err;
    auto *saved_out = std::cout.rdbuf(out.rdbuf());
    auto *saved_err = std::cerr.rdbuf(err.rdbuf());
    Run result;
    try {
        paw::cli::config(static_cast<int>(arguments.size()), argv.data());
    } catch (const std::exception &error) {
        result.error = error.what();
    }
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);
    result.out = out.str();
    result.err = err.str();
    return result;
}

std::string contents(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

// A script the editor variables can name as `sh SCRIPT`.
std::string script(const fs::path &path, const std::string &body) {
    std::ofstream(path) << body;
    return "sh " + path.string();
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: config_command_tests EXAMPLE_CONFIG\n";
        return 2;
    }
    std::string pattern = (fs::temp_directory_path() / "paw-config-command-XXXXXX").string();
    if (!mkdtemp(pattern.data())) {
        std::cerr << "cannot make a temporary directory\n";
        return 2;
    }
    fs::path root = pattern, config = root / "with space";
    fs::path personal = config / "paw/init.lua", log = root / "log";
    fs::create_directories(root / "state");
    setenv("PAW_DEFAULT_CONFIG", argv[1], 1);
    setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);
    setenv("XDG_CONFIG_HOME", config.c_str(), 1);
    unsetenv("VISUAL");
    unsetenv("EDITOR");
    CHECK(paw::cli::personal_configuration_path() == personal);

    // Without a personal configuration, the default one, and how to make one.
    auto shown = run({});
    CHECK(shown.error.empty(), "%s", shown.error.c_str());
    CHECK(shown.out == std::string(argv[1]) + "\n", "%s", shown.out.c_str());
    CHECK(shown.err.find("paw config edit makes one") != std::string::npos, "%s",
          shown.err.c_str());

    CHECK(run({"bogus"}).error == "usage: paw config [edit]");
    CHECK(run({"edit", "more"}).error == "usage: paw config [edit]");

    // edit makes the personal configuration and opens it, its path one argument.
    std::string logging = script(root / "log.sh", "printf '%s\\n' \"$#\" \"$@\" > '" +
                                                      log.string() + "'\n");
    setenv("EDITOR", logging.c_str(), 1);
    auto made = run({"edit"});
    CHECK(made.error.empty(), "%s", made.error.c_str());
    CHECK(contents(personal) == paw::cli::starter_configuration());
    CHECK(contents(log) == "1\n" + personal.string() + "\n", "%s", contents(log).c_str());
    CHECK(made.err.find("Made " + personal.string()) != std::string::npos, "%s",
          made.err.c_str());
    CHECK(paw::cli::starter_configuration().find(argv[1]) != std::string::npos);
    try {
        auto loaded = paw::load_config(personal);
        CHECK(!loaded.bindings.empty());
    } catch (const std::exception &error) {
        CHECK(false, "the starter configuration does not load: %s", error.what());
    }
    struct sigaction interrupt{};
    sigaction(SIGINT, nullptr, &interrupt);
    CHECK(interrupt.sa_handler == SIG_DFL, "Ctrl+C is still ignored after the editor");

    // Once it exists, that is the one shown, and opening it again neither makes nor says more.
    shown = run({});
    CHECK(shown.out == personal.string() + "\n", "%s", shown.out.c_str());
    CHECK(shown.err.empty(), "%s", shown.err.c_str());
    auto again = run({"edit"});
    CHECK(again.error.empty() && again.err.empty(), "%s%s", again.error.c_str(),
          again.err.c_str());

    // A file saved with a mistake is reported; one the editor left alone is not checked.
    setenv("EDITOR",
           script(root / "break.sh", "printf 'return { version = 1, nonsense = 2 }\\n' > \"$1\"\n")
               .c_str(),
           1);
    auto broken = run({"edit"});
    CHECK(broken.error.find("nonsense") != std::string::npos, "%s", broken.error.c_str());
    setenv("EDITOR", "true", 1);
    CHECK(run({"edit"}).error.empty());

    // $VISUAL comes before $EDITOR.
    setenv("VISUAL",
           script(root / "fix.sh", "printf 'return { version = 1, extends = \"default\", "
                                   "terminal = { \"foot\" } }\\n' > \"$1\"\n")
               .c_str(),
           1);
    setenv("EDITOR", "false", 1);
    auto fixed = run({"edit"});
    CHECK(fixed.error.empty(), "%s", fixed.error.c_str());
    CHECK(contents(personal).find("foot") != std::string::npos);
    unsetenv("VISUAL");

    // An editor that fails, or is not there.
    CHECK(run({"edit"}).error == "the editor false exited with status 1");
    setenv("EDITOR", "paw-no-such-editor", 1);
    auto missing = run({"edit"});
    CHECK(missing.error.starts_with("cannot run the editor paw-no-such-editor"), "%s",
          missing.error.c_str());

    // Without $XDG_CONFIG_HOME or $HOME there is nowhere to make one.
    unsetenv("XDG_CONFIG_HOME");
    const char *home = std::getenv("HOME");
    std::string saved_home = home ? home : "";
    unsetenv("HOME");
    shown = run({});
    CHECK(shown.out == std::string(argv[1]) + "\n" && shown.err.empty(), "%s%s",
          shown.out.c_str(), shown.err.c_str());
    CHECK(run({"edit"}).error.starts_with("cannot tell where the configuration lives"));
    if (!saved_home.empty())
        setenv("HOME", saved_home.c_str(), 1);

    fs::remove_all(root);
    return failures ? 1 : 0;
}
