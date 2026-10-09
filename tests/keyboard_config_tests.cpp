// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the keyboard: XKB names and rules, and keymap files.
#include "paw/config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
// The error `source` gives; fails the test when it is accepted.
static std::string error_of(const std::string &source) {
    try {
        (void)paw::parse_config(source, "@init.lua");
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
static void rejects(const std::string &source, const std::string &fragment) {
    auto message = error_of(source);
    require(message.find(fragment) != std::string::npos,
            "expected '" + fragment + "' in: " + message);
}

static void rules() {
    auto defaults = paw::parse_config("return {}");
    require(std::string(defaults.settings.keyboard_rules).empty(), "rules default to xkbcommon's");
    auto evdev = paw::parse_config("return {keyboard={rules='evdev',layout='us'}}");
    require(std::string(evdev.settings.keyboard_rules) == "evdev", "keyboard.rules not parsed");
    rejects("return {keyboard={rules=3}}", "rules must be a string");
    rejects("return {keyboard={rules='no-such-rules-zz'}}", "keyboard");
}

// Names XKB has no keymap for are refused with its reason, at the keyboard table.
static void diagnostics() {
    rejects("return {\n  layout = { gap = 2 },\n  keyboard = {\n    layout = 'zz-no-such',\n  },\n}",
            "init.lua:3: XKB has no keymap");
    rejects("return {keyboard={layout='zz-no-such'}}", "symbols/zz-no-such");
    rejects("return {keyboard={layout='us',variant='zz-no-such'}}", "zz-no-such");
}

// A directory of its own for configuration and keymap files, removed afterwards.
struct Scratch {
    fs::path root;
    Scratch() {
        auto pattern = (fs::temp_directory_path() / "paw-keyboard-XXXXXX").string();
        if (!mkdtemp(pattern.data()))
            throw std::runtime_error("cannot make a temporary directory");
        root = pattern;
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    fs::path write(const std::string &name, const std::string &text) const {
        auto path = root / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
        return path;
    }
};
// A keymap with two layouts, the first renamed so a test can tell it came from the file.
const std::string good_keymap = "xkb_keymap {\n"
                                "  xkb_keycodes { include \"evdev\" };\n"
                                "  xkb_types { include \"complete\" };\n"
                                "  xkb_compat { include \"complete\" };\n"
                                "  xkb_symbols { include \"pc+us+no:2\" name[Group1] = \"Testish\"; };\n"
                                "};\n";
// The same with a syntax error on line 5, column 43.
const std::string broken_keymap = "xkb_keymap {\n"
                                  "  xkb_keycodes { include \"evdev\" };\n"
                                  "  xkb_types { include \"complete\" };\n"
                                  "  xkb_compat { include \"complete\" };\n"
                                  "  xkb_symbols { include \"pc+us+no:2\" oops };\n"
                                  "};\n";
// `keyboard = { file = FILE }` on line 3 of an init.lua.
std::string with_file(const std::string &file) {
    return "return {\n  keyboard = {\n    file = \"" + file + "\",\n    layout = \"no\",\n  },\n}\n";
}

static void keymap_files() {
    Scratch scratch;
    auto keymap = scratch.write("keymap.xkb", good_keymap);
    // Relative to the configuration file, wherever paw runs from.
    auto init = scratch.write("init.lua", with_file("keymap.xkb"));
    auto config = paw::load_config(init);
    require(fs::path(config.settings.keyboard_file) == keymap,
            std::string("keyboard.file not made absolute: ") + config.settings.keyboard_file);
    require(std::string(config.settings.keyboard_layout) == "no",
            "the names stay set beside a file, to stand in for it");
    init = scratch.write("nested/init.lua", with_file("../keymaps/../keymap.xkb"));
    require(fs::path(paw::load_config(init).settings.keyboard_file) == keymap,
            "a relative path is not taken from the configuration's directory");
    init = scratch.write("init.lua", with_file(keymap.string()));
    require(fs::path(paw::load_config(init).settings.keyboard_file) == keymap,
            "an absolute path changed");
    const char *home = std::getenv("HOME");
    const std::string saved = home ? home : "";
    setenv("HOME", scratch.root.c_str(), 1);
    init = scratch.write("init.lua", with_file("~/keymap.xkb"));
    require(fs::path(paw::load_config(init).settings.keyboard_file) == keymap,
            "~/ is not the home directory");
    unsetenv("HOME");
    try {
        (void)paw::load_config(init);
        throw std::runtime_error("~/ without HOME was accepted");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()).find("HOME is not set") != std::string::npos,
                std::string("~/ without HOME: ") + error.what());
    }
    if (home)
        setenv("HOME", saved.c_str(), 1);
    require(std::string(paw::parse_config("return {keyboard={file=''}}").settings.keyboard_file)
                .empty(),
            "an empty keyboard.file is not unset");
    rejects("return {keyboard={file=3}}", "keyboard.file must be a string");

    // What is wrong, with the line of the setting and, where xkbcommon knows it, of the keymap.
    auto fails = [&](const std::string &file, const std::string &text) {
        auto path = scratch.root / "init.lua";
        std::ofstream(path, std::ios::binary | std::ios::trunc) << with_file(file);
        try {
            (void)paw::load_config(path);
        } catch (const std::exception &error) {
            std::string message = error.what();
            require(message.find("init.lua:3: keyboard.file: ") != std::string::npos &&
                        message.find(text) != std::string::npos,
                    "expected '" + text + "' at init.lua:3 in: " + message);
            return message;
        }
        throw std::runtime_error("keyboard.file " + file + " was accepted");
    };
    fails("missing.xkb", "cannot read " + (scratch.root / "missing.xkb").string() +
                             ": No such file or directory");
    auto broken = scratch.write("broken.xkb", broken_keymap);
    fails("broken.xkb", broken.string() + ":5:43: syntax error");
    scratch.write("include.xkb", "xkb_keymap {\n  xkb_keycodes { include \"evdev\" };\n"
                                 "  xkb_types { include \"complete\" };\n"
                                 "  xkb_compat { include \"complete\" };\n"
                                 "  xkb_symbols { include \"pc+zz-no-such\" };\n};\n");
    fails("include.xkb", (scratch.root / "include.xkb").string() +
                             ": Couldn't find file \"symbols/zz-no-such\"");
    scratch.write("empty.xkb", "");
    fails("empty.xkb", (scratch.root / "empty.xkb").string() + ": ");
    fails(".", "cannot read");

    // A session starting with it, or reloading it, gets the default configuration instead,
    // whose keymap comes from the names, and the red bar says what is wrong and where.
    auto init_path = scratch.write("init.lua", with_file("broken.xkb"));
    std::string error;
    auto fallback = paw::load_config_or_default(init_path, error);
    require(std::string(fallback.settings.keyboard_file).empty() &&
                !std::string(fallback.settings.keyboard_layout).empty(),
            "a broken keymap file did not leave the default keyboard settings");
    require(error.find("init.lua:3: keyboard.file: " + broken.string() + ":5:43") !=
                std::string::npos,
            "the fallback's error lacks the file and line: " + error);
}

// switch_layout's binding field: "next" (the default), "prev", or a number from 1.
static void switch_layout() {
    auto layout_of = [](const std::string &field) {
        auto config = paw::parse_config(
            "return {bindings={{mods={'Super','Alt'},key='space',action='switch_layout'" + field +
            "}}}");
        require(config.bindings.size() == 1 && config.bindings[0].action == SH_SWITCH_LAYOUT,
                "switch_layout binding not parsed");
        return config.bindings[0].layout;
    };
    require(layout_of("") == 0, "switch_layout does not default to the next layout");
    require(layout_of(",layout='next'") == 0 && layout_of(",layout='prev'") == -1,
            "next and prev not parsed");
    require(layout_of(",layout=2") == 2 && layout_of(",layout='3'") == 3,
            "a layout's number not parsed");
    rejects("return {bindings={{key='a',action='switch_layout',layout='sideways'}}}",
            "switch_layout takes \"next\", \"prev\", or a layout's number");
    rejects("return {bindings={{key='a',action='switch_layout',layout=0}}}",
            "between 1 and 32");
    rejects("return {bindings={{key='a',action='switch_layout',layout=33}}}",
            "between 1 and 32");
    rejects("return {bindings={{key='a',action='switch_layout',layout=true}}}",
            "layout must be \"next\", \"prev\" or a number, not a boolean");
    rejects("return {bindings={{key='a',action='close',layout=2}}}",
            "layout is only valid with switch_layout");
    require(paw::parse_layout_choice("next") == 0 && paw::parse_layout_choice("2") == 2,
            "control requests are not parsed as bindings are");
    for (const char *bad : {"", "0", "-1", "2x", "33", "first"}) {
        try {
            (void)paw::parse_layout_choice(bad);
            throw std::runtime_error(std::string("accepted switch_layout ") + bad);
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()).find("switch_layout takes") != std::string::npos,
                    std::string("switch_layout ") + bad + ": " + error.what());
        }
    }
}

// keyboard.shortcuts_inhibit: whether applications may have the keys the bindings take.
static void shortcuts_inhibit() {
    require(paw::parse_config("return {}").settings.shortcuts_inhibit,
            "applications may not inhibit shortcuts by default");
    require(!paw::parse_config("return {keyboard={shortcuts_inhibit=false}}")
                 .settings.shortcuts_inhibit,
            "keyboard.shortcuts_inhibit = false not parsed");
    rejects("return {keyboard={shortcuts_inhibit='no'}}",
            "keyboard.shortcuts_inhibit must be a boolean");
}

int main() {
    try {
        rules();
        diagnostics();
        keymap_files();
        switch_layout();
        shortcuts_inhibit();
        std::cout << "Keyboard configuration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
