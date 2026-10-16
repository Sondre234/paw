// SPDX-License-Identifier: GPL-3.0-or-later
// power.lock_style: the stock swaylock's arguments in the appearance profile's look.
#include "paw/lock_style.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <wordexp.h>

#include "check.h"

namespace {
// The value given to `option` in `command`, "" when it is not there; "(flag)" for one without.
std::string value(const paw::Command &command, const std::string &option) {
    auto it = std::find(command.begin(), command.end(), option);
    if (it == command.end())
        return "";
    if (it + 1 == command.end() || (it + 1)->starts_with("--"))
        return "(flag)";
    return *(it + 1);
}

// Every value given to `option` in `command`, in order.
std::vector<std::string> values(const paw::Command &command, const std::string &option) {
    std::vector<std::string> found;
    for (auto it = command.begin(); it != command.end(); ++it)
        if (*it == option && it + 1 != command.end())
            found.push_back(*(it + 1));
    return found;
}

// Every colour swaylock 1.8 takes besides the background, so that none stays its own.
const char *const colors[] = {
    "--bs-hl-color",          "--caps-lock-bs-hl-color", "--caps-lock-key-hl-color",
    "--inside-color",         "--inside-clear-color",    "--inside-caps-lock-color",
    "--inside-ver-color",     "--inside-wrong-color",    "--key-hl-color",
    "--layout-bg-color",      "--layout-border-color",   "--layout-text-color",
    "--line-color",           "--line-clear-color",      "--line-caps-lock-color",
    "--line-ver-color",       "--line-wrong-color",      "--ring-color",
    "--ring-clear-color",     "--ring-caps-lock-color",  "--ring-ver-color",
    "--ring-wrong-color",     "--separator-color",       "--text-color",
    "--text-clear-color",     "--text-caps-lock-color",  "--text-ver-color",
    "--text-wrong-color",
};

int distance(const std::string &a, const std::string &b) {
    int sum = 0;
    for (int i = 0; i < 6; i += 2)
        sum += std::abs(std::stoi(a.substr(i, 2), nullptr, 16) -
                        std::stoi(b.substr(i, 2), nullptr, 16));
    return sum;
}

// What swaylock makes of an --image argument: its output name dropped, a backslash put between
// two spaces, then wordexp, without running commands should the escape miss one.
std::string swaylock_image(std::string argument, bool &ok) {
    ok = false;
    auto colon = argument.find(':');
    if (colon == std::string::npos)
        return "";
    std::string path = argument.substr(colon + 1);
    for (size_t at; (at = path.find("  ")) != std::string::npos;)
        path.insert(at + 1, "\\");
    wordexp_t words;
    if (wordexp(path.c_str(), &words, WRDE_NOCMD | WRDE_UNDEF) != 0)
        return "";
    std::string joined;
    for (size_t i = 0; i < words.we_wordc; ++i)
        joined += (i ? " " : "") + std::string(words.we_wordv[i]);
    wordfree(&words);
    ok = true;
    return joined;
}

paw::LockStyleContext context(const std::filesystem::path &config_path = "/home/u/.config/paw/init.lua") {
    paw::LockStyleContext context;
    context.config_path = config_path;
    context.readable = [](const std::filesystem::path &) { return true; };
    return context;
}

// washi's colours, written out here so that a change to the shipped rice changes nothing.
const char *const washi = R"(return {
    appearance = { background = "#f4f0f3" },
    windows = { border_color = { "#8fc1ec", "#f2a0be", angle = 135 }, urgent_color = "#d9608c" },
    shell = { accent = "#5e9edb", panel_color = "#f0ecf0", text_color = "#363c58",
              font = "Space Grotesk", wallpaper = "rices/washi/washi.jpg" },
})";
} // namespace

int main(int argc, char **argv) {
    // The colours mapped: the panel inside the ring, the accent the ring, the border's stop that
    // stands out from it a key press, the urgent colour a backspace and a wrong password.
    {
        auto config = paw::parse_config(washi);
        auto command = paw::lock_command(config, context());
        CHECK(command.size() > 2 && command[0] == "swaylock" && command[1] == "-f",
              "the stock command does not come first");
        CHECK(value(command, "--image") == ":/home/u/.config/paw/rices/washi/washi.jpg",
              "image %s", value(command, "--image").c_str());
        CHECK(value(command, "--scaling") == "fill");
        CHECK(value(command, "--color") == "f4f0f3ff", "%s", value(command, "--color").c_str());
        CHECK(value(command, "--font") == "Space Grotesk");
        CHECK(value(command, "--indicator-idle-visible") == "(flag)");
        CHECK(value(command, "--show-failed-attempts") == "(flag)");
        for (const char *option : {"--inside-color", "--inside-clear-color",
                                   "--inside-caps-lock-color", "--inside-ver-color",
                                   "--inside-wrong-color", "--layout-bg-color"})
            CHECK(value(command, option) == "f0ecf0ff", "%s %s", option,
                  value(command, option).c_str());
        for (const char *option :
             {"--ring-color", "--ring-clear-color", "--ring-caps-lock-color", "--ring-ver-color"})
            CHECK(value(command, option) == "5e9edbff", "%s %s", option,
                  value(command, option).c_str());
        for (const char *option : {"--ring-wrong-color", "--bs-hl-color", "--caps-lock-bs-hl-color"})
            CHECK(value(command, option) == "d9608cff", "%s %s", option,
                  value(command, option).c_str());
        // The pink stop, as the blue one is close to the blue ring.
        CHECK(value(command, "--key-hl-color") == "f2a0beff", "%s",
              value(command, "--key-hl-color").c_str());
        CHECK(value(command, "--caps-lock-key-hl-color") == "f2a0beff");
        for (const char *option : {"--text-color", "--text-clear-color", "--text-caps-lock-color",
                                   "--text-ver-color", "--text-wrong-color",
                                   "--layout-text-color"})
            CHECK(value(command, option) == "363c58ff", "%s %s", option,
                  value(command, option).c_str());
        for (const char *option : {"--line-color", "--line-wrong-color", "--separator-color",
                                   "--layout-border-color"})
            CHECK(value(command, option) == "00000000", "%s %s", option,
                  value(command, option).c_str());
        for (const char *option : colors)
            CHECK(!value(command, option).empty(), "%s left at swaylock's own", option);
    }
    // A see-through panel (mochi's glass) gives an inside no more see-through than the glass's
    // floor; a single border colour that is the accent gives the text's colour to key presses.
    {
        auto config = paw::parse_config(R"(return {
            windows = { border_color = { "#cba6f7", "#f5c2e7" } },
            shell = { accent = "#cba6f7", panel_color = "#1e1e2ea6", text_color = "#cdd6f4" },
        })");
        auto command = paw::lock_command(config, context());
        CHECK(value(command, "--inside-color") == "1e1e2ebf", "%s",
              value(command, "--inside-color").c_str());
        CHECK(value(command, "--key-hl-color") == "f5c2e7ff");
        CHECK(value(command, "--font").empty(), "a font named without shell.font");
        auto plain = paw::parse_config(R"(return {
            windows = { border_color = "#3d6fd9" },
            shell = { accent = "#3d6fd9", panel_color = "#f4f6faee", text_color = "#1b2230" },
        })");
        command = paw::lock_command(plain, context());
        CHECK(value(command, "--key-hl-color") == "1b2230ff", "%s",
              value(command, "--key-hl-color").c_str());
        CHECK(value(command, "--inside-color") == "f4f6faee", "an opaque enough panel changed");
        // The defaults: the default border is the default accent too.
        command = paw::lock_command(paw::parse_config("return {}"), context());
        CHECK(value(command, "--key-hl-color") == "edf2faff", "%s",
              value(command, "--key-hl-color").c_str());
        CHECK(value(command, "--color") == "19212eff", "%s", value(command, "--color").c_str());
        CHECK(value(command, "--image").empty(), "an image without a wallpaper");
    }
    // Only the stock command, switched on, without a swaylock configuration, is touched.
    {
        auto config = paw::parse_config(washi);
        CHECK(config.power.lock_style, "power.lock_style is off by default");
        auto custom = paw::parse_config(
            "return {power={lock_command={'swaylock','-f','-c','000000'}}}");
        CHECK((paw::lock_command(custom, context()) ==
               paw::Command{"swaylock", "-f", "-c", "000000"}));
        custom = paw::parse_config("return {power={lock_command={'swaylock'}}}");
        CHECK((paw::lock_command(custom, context()) == paw::Command{"swaylock"}));
        custom = paw::parse_config("return {power={lock_command={'gtklock','-d'}}}");
        CHECK((paw::lock_command(custom, context()) == paw::Command{"gtklock", "-d"}));
        custom = paw::parse_config("return {power={lock_command={}}}");
        CHECK(paw::lock_command(custom, context()).empty());
        auto off = paw::parse_config("return {power={lock_style=false}}");
        CHECK(!off.power.lock_style);
        CHECK(paw::lock_command(off, context()) == paw::stock_lock_command());
        auto configured = context();
        configured.swaylock_config = true;
        CHECK(paw::lock_command(config, configured) == paw::stock_lock_command(),
              "a swaylock configuration was overridden");
    }
    // A wallpaper that is missing, or that no check was given for, leaves the background colour.
    {
        auto config = paw::parse_config(washi);
        auto missing = context();
        missing.readable = [](const std::filesystem::path &) { return false; };
        auto command = paw::lock_command(config, missing);
        CHECK(value(command, "--image").empty() && value(command, "--scaling").empty(),
              "a missing wallpaper was passed");
        CHECK(value(command, "--color") == "f4f0f3ff");
        missing.readable = nullptr;
        CHECK(value(paw::lock_command(config, missing), "--image").empty());
    }
    // The wallpaper as the shell shows it: picked from the panel for the profile while the
    // profile configures what it did then; the last line for a profile counts.
    {
        auto config = paw::parse_config(washi);
        const std::filesystem::path path = "/c/init.lua";
        CHECK(paw::lock_wallpaper(config, path, "") == "/c/rices/washi/washi.jpg");
        config.profile = "washi";
        std::string picked = "sumi\trices/washi/washi.jpg\t/p/sumi.png\n"
                             "washi\trices/washi/washi.jpg\t/p/first.png\n"
                             "washi\trices/washi/washi.jpg\t/p/picked.png\n"
                             "broken line\n"
                             "washi\t\t\n";
        CHECK(paw::lock_wallpaper(config, path, picked) == "/p/picked.png", "%s",
              paw::lock_wallpaper(config, path, picked).c_str());
        // Picked for another wallpaper the profile no longer configures.
        CHECK(paw::lock_wallpaper(config, path, "washi\told.jpg\t/p/picked.png") ==
              "/c/rices/washi/washi.jpg");
        // A later line for the old wallpaper hides an earlier one, as the shell's map does.
        CHECK(paw::lock_wallpaper(config, path,
                                  "washi\trices/washi/washi.jpg\t/p/a.png\nwashi\told\t/p/b.png") ==
              "/c/rices/washi/washi.jpg");
        config.shell.wallpaper = "/abs/w.jpg";
        CHECK(paw::lock_wallpaper(config, path, "") == "/abs/w.jpg");
        config.shell.wallpaper.clear();
        CHECK(paw::lock_wallpaper(config, path, "").empty());
        CHECK(paw::lock_wallpaper(config, path, "washi\t\t/p/picked.png\n") == "/p/picked.png");
        config.profile.clear();
        CHECK(paw::lock_wallpaper(config, path, "\t\t/p/none.png") == "/p/none.png",
              "the picture picked without a profile");
        // The picked file reaches the command, and the check is asked about it.
        config.profile = "washi";
        auto picking = context(path);
        picking.picked_wallpapers = "washi\t\t/p/picked.png\n";
        std::filesystem::path asked;
        picking.readable = [&](const std::filesystem::path &file) {
            asked = file;
            return true;
        };
        CHECK(value(paw::lock_command(config, picking), "--image") == ":/p/picked.png");
        CHECK(asked == "/p/picked.png");
    }
    // Each monitor's own wallpaper, configured for it or picked for it alone, while the
    // configuration gives the monitor what it did then; the lock screen shows it on that monitor,
    // as OUTPUT:PATH after every monitor's, unless it is every monitor's anyway, it cannot be read
    // or swaylock cannot name the monitor (a colon in its name).
    {
        auto config = paw::parse_config(washi);
        config.profile = "washi";
        config.shell.monitor_wallpapers = {{"DP-1", "/w/left.jpg"},
                                           {"HDMI-A-1", "rices/washi/washi.jpg"},
                                           {"odd:name", "/w/odd.jpg"}};
        const std::filesystem::path path = "/c/init.lua";
        CHECK(paw::lock_wallpaper(config, path, "", "DP-1") == "/w/left.jpg");
        CHECK(paw::lock_wallpaper(config, path, "", "eDP-1") == "/c/rices/washi/washi.jpg");
        CHECK(paw::lock_wallpaper(config, path, "", "HDMI-A-1") == "/c/rices/washi/washi.jpg");
        const std::string picked = "washi\trices/washi/washi.jpg\t/p/every.png\n"
                                   "washi\t/w/left.jpg\t/p/left.png\tDP-1\n"
                                   "washi\trices/washi/washi.jpg\t/p/right.png\tHDMI-A-1\n"
                                   "washi\told.jpg\t/p/stale.png\teDP-1\n"
                                   "sumi\trices/washi/washi.jpg\t/p/sumi.png\tDP-2\n"
                                   "washi\trices/washi/washi.jpg\t/p/blank.png\t\n"
                                   "washi\trices/washi/washi.jpg\t/p/five.png\tDP-3\textra\n";
        CHECK(paw::lock_wallpaper(config, path, picked) == "/p/every.png");
        CHECK(paw::lock_wallpaper(config, path, picked, "DP-1") == "/p/left.png");
        CHECK(paw::lock_wallpaper(config, path, picked, "HDMI-A-1") == "/p/right.png");
        // Picked for a wallpaper the monitor no longer has, or for another profile: every
        // monitor's.
        CHECK(paw::lock_wallpaper(config, path, picked, "eDP-1") == "/p/every.png");
        CHECK(paw::lock_wallpaper(config, path, picked, "DP-2") == "/p/every.png");
        CHECK(paw::lock_wallpaper(config, path, picked, "DP-3") == "/p/every.png");
        // A monitor with a wallpaper configured for it does not take one picked for every
        // monitor while shell.wallpaper was what it is.
        CHECK(paw::lock_wallpaper(config, path, "washi\trices/washi/washi.jpg\t/p/every.png\n",
                                  "DP-1") == "/w/left.jpg");
        auto picking = context(path);
        picking.picked_wallpapers = picked;
        auto command = paw::lock_command(config, picking);
        CHECK((values(command, "--image") ==
               std::vector<std::string>{":/p/every.png", "DP-1:/p/left.png", "HDMI-A-1:/p/right.png"}),
              "images %s", [&] {
                  std::string all;
                  for (const auto &image : values(command, "--image"))
                      all += image + " ";
                  return all;
              }().c_str());
        CHECK(values(command, "--scaling") == std::vector<std::string>{"fill"});
        // As swaylock reads them: the output's name to the first colon, the path after it.
        bool ok = false;
        CHECK(swaylock_image(values(command, "--image")[1], ok) == "/p/left.png" && ok);
        // A monitor's own picture that cannot be read leaves it every monitor's.
        picking.readable = [](const std::filesystem::path &file) { return file != "/p/left.png"; };
        CHECK((values(paw::lock_command(config, picking), "--image") ==
               std::vector<std::string>{":/p/every.png", "HDMI-A-1:/p/right.png"}));
        // The same as every monitor's: once.
        picking.readable = [](const std::filesystem::path &) { return true; };
        picking.picked_wallpapers = "washi\trices/washi/washi.jpg\t/p/every.png\n"
                                    "washi\t/w/left.jpg\t/p/every.png\tDP-1\n";
        CHECK((values(paw::lock_command(config, picking), "--image") ==
               std::vector<std::string>{":/p/every.png"}));
        // No wallpaper for every monitor: the monitors' own alone, still filling them.
        config.shell.wallpaper.clear();
        config.shell.monitor_wallpapers = {{"DP-1", "/w/left.jpg"}};
        command = paw::lock_command(config, context(path));
        CHECK((values(command, "--image") == std::vector<std::string>{"DP-1:/w/left.jpg"}));
        CHECK(value(command, "--scaling") == "fill");
    }
    // Paths swaylock's wordexp gives back as they are, and runs nothing from.
    {
        for (std::string path :
             {"/w/plain.jpg", "/w/a b.jpg", "/w/a  b   c.jpg", "/w/$(touch pwned).jpg",
              "/w/`id`.jpg", "/w/${HOME}.jpg", "/w/'single'.jpg", "/w/\"double\".jpg",
              "/w/back\\slash.jpg", "/w/glob*?[ab].jpg", "/w/~tilde.jpg", "/w/{a,b}.jpg",
              "/w/a;b|c&d<e>f(g).jpg", "/w/#hash.jpg", "/w/和紙 の 猫.jpg", "/w/a:b.jpg",
              "/w/tab\there.jpg", "/w/100% x=y,z+@.jpg", "/w/trailing\\", "/w/!bang.jpg",
              // wordexp expands a tilde after = or, in a word with =, after a colon
              "/w/a=~root:~root.jpg"}) {
            auto escaped = paw::wordexp_escape(path);
            CHECK(escaped.has_value(), "%s not escaped", path.c_str());
            if (!escaped)
                continue;
            bool ok = false;
            auto back = swaylock_image(":" + *escaped, ok);
            CHECK(ok && back == path, "%s came back as %s (from %s)", path.c_str(), back.c_str(),
                  escaped->c_str());
        }
        CHECK(!paw::wordexp_escape("/w/new\nline.jpg"), "a newline was escaped");
        // And through the command: a colon in the path stays in the path.
        auto config = paw::parse_config(washi);
        config.shell.wallpaper = "/w/a:b c.jpg";
        bool ok = false;
        auto argument = value(paw::lock_command(config, context()), "--image");
        CHECK(swaylock_image(argument, ok) == "/w/a:b c.jpg" && ok, "%s", argument.c_str());
        config.shell.wallpaper = "/w/new\nline.jpg";
        CHECK(value(paw::lock_command(config, context()), "--image").empty(),
              "a path wordexp cannot keep was passed");
    }
    // The shipped profiles: every colour set, key presses that show on the ring, and text that
    // shows on the inside.
    if (argc > 1) {
        std::ifstream example(argv[1]);
        std::stringstream source;
        source << example.rdbuf();
        const auto directory = std::filesystem::path(argv[1]).parent_path();
        auto names = paw::parse_config(source.str(), argv[1], directory).profiles;
        CHECK(names.size() >= 3, "the example's profiles were not found");
        for (const auto &name : names) {
            auto config = paw::parse_config(source.str(), argv[1], directory, name);
            auto command = paw::lock_command(config, context(argv[1]));
            for (const char *option : colors)
                CHECK(!value(command, option).empty(), "%s: %s left at swaylock's own",
                      name.c_str(), option);
            auto ring = value(command, "--ring-color"), key = value(command, "--key-hl-color");
            CHECK(distance(ring, key) >= 64, "%s: key presses (%s) hardly show on the ring (%s)",
                  name.c_str(), key.c_str(), ring.c_str());
            auto inside = value(command, "--inside-color"), text = value(command, "--text-color");
            CHECK(distance(inside, text) >= 200, "%s: text (%s) hardly shows on the inside (%s)",
                  name.c_str(), text.c_str(), inside.c_str());
            CHECK(std::stoi(inside.substr(6, 2), nullptr, 16) >= 191, "%s: inside %s",
                  name.c_str(), inside.c_str());
            if (!config.shell.wallpaper.empty())
                CHECK(!value(command, "--image").empty(), "%s: no image", name.c_str());
        }
    }
    if (failures)
        std::cerr << failures << " check(s) failed\n";
    return failures ? 1 : 0;
}
