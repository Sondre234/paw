// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/config.hpp"
#include "paw/child_trial.hpp"
#include "paw/effects.h"
#include "paw/cat_sprite.h"
#include "paw/config_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <linux/input-event-codes.h>
#include <lua.hpp>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <xkbcommon/xkbcommon.h>

namespace paw {
namespace {
// What a configuration's Lua may hold at once while it runs: many times what one needs, and
// far less than would starve the session. The instruction budget cannot stop a single call that
// asks for much, such as ("x"):rep(2^31 - 1), 2 GiB at once.
constexpr size_t memory_limit = 64u << 20;
struct Memory {
    size_t used = 0;
    bool limited = false; // while the configuration's own code runs (evaluate)
    bool refused = false;
};
void *allocate(void *data, void *block, size_t old, size_t size) {
    auto &memory = *static_cast<Memory *>(data);
    if (!block)
        old = 0; // the kind of object being made, not a size
    if (size == 0) {
        std::free(block);
        memory.used -= old;
        return nullptr;
    }
    if (memory.limited && size > old &&
        size - old > memory_limit - std::min(memory.used, memory_limit)) {
        memory.refused = true;
        return nullptr;
    }
    void *result = std::realloc(block, size);
    if (result)
        memory.used = memory.used - old + size;
    return result;
}
struct Close {
    void operator()(lua_State *L) const {
        void *memory = nullptr;
        lua_getallocf(L, &memory);
        lua_close(L);
        delete static_cast<Memory *>(memory);
    }
};
using State = std::unique_ptr<lua_State, Close>;
Memory &memory_of(lua_State *L) {
    void *memory = nullptr;
    lua_getallocf(L, &memory);
    return *static_cast<Memory *>(memory);
}
// Section being read ("shell"), so an error can be placed in the source file.
thread_local std::string current_section;
struct ConfigError : std::runtime_error {
    std::string text;               // the message without "configuration: " or a location
    std::vector<std::string> trail; // names to look for in the source, outermost first
    ConfigError(std::string message, std::vector<std::string> names)
        : std::runtime_error("configuration: " + message), text(std::move(message)),
          trail(std::move(names)) {}
};
// Each table of the current section, then `leaf`: "shell", "thumbnails" for shell.thumbnails;
// "outputs", "monitors", "DP-1" for outputs.monitors["DP-1"]; an entry of a list by number
// (windows.rules[2]) has no name in the source, and is left out.
std::vector<std::string> trail_to(const std::string &leaf) {
    std::vector<std::string> names;
    const auto &path = current_section;
    for (size_t at = 0; at < path.size();) {
        if (path[at] == '.') {
            ++at;
        } else if (path.compare(at, 2, "[\"") == 0) {
            auto close = std::min(path.find("\"]", at + 2), path.size());
            names.push_back(path.substr(at + 2, close - at - 2));
            at = close + 2;
        } else if (path[at] == '[') {
            at = std::min(path.find(']', at), path.size()) + 1;
        } else {
            auto end = std::min(path.find_first_of(".[", at), path.size());
            names.push_back(path.substr(at, end - at));
            at = end;
        }
    }
    if (!leaf.empty())
        names.push_back(leaf);
    return names;
}
// `leaf` is the setting name to point at in the source, when there is one.
[[noreturn]] void fail(const std::string &message, const std::string &leaf = "") {
    throw ConfigError(message, trail_to(leaf));
}
std::string last_name(const std::string &label) {
    return label.substr(label.find_last_of('.') == std::string::npos ? 0
                                                                      : label.find_last_of('.') + 1);
}
// "a string", "a number", ... for what Lua holds at `index`.
std::string described(lua_State *L, int index) {
    switch (lua_type(L, index)) {
    case LUA_TNIL: return "nothing";
    case LUA_TBOOLEAN: return "a boolean";
    case LUA_TNUMBER: return lua_isinteger(L, index) ? "an integer" : "a non-integer number";
    case LUA_TSTRING: return "a string";
    case LUA_TTABLE: return "a table";
    default: return std::string("a ") + luaL_typename(L, index);
    }
}
// "shell.panel_height" for `panel_height` read inside shell.
std::string qualified(const std::string &label) {
    if (current_section.empty() || label.find('.') != std::string::npos)
        return label;
    return current_section + "." + label;
}
[[noreturn]] void wrong_type(lua_State *L, const std::string &label, const char *wanted) {
    fail(qualified(label) + " must be " + wanted + ", not " + described(L, -1), last_name(label));
}
[[noreturn]] void out_of_range(const std::string &key, double value, double min, double max) {
    auto text = [](double number) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%g", number);
        return std::string(buffer);
    };
    fail(qualified(key) + " must be between " + text(min) + " and " + text(max) + ", not " + text(value), key);
}
void table(lua_State *L, int index, const char *label) {
    if (!lua_istable(L, index)) {
        lua_pushvalue(L, index);
        auto kind = described(L, -1);
        lua_pop(L, 1);
        fail(std::string(label) + " must be a table, not " + kind, last_name(label));
    }
}
[[noreturn]] void unknown(const std::string &what, const std::string &name,
                          const std::vector<std::string> &valid, const std::string &leaf = "",
                          const std::string &where = "") {
    auto guess = closest_match(name, valid);
    // A name too long to read is quoted by its start.
    auto shown = name.size() > 64 ? name.substr(0, 64) + "..." : name;
    fail("unknown " + what + " '" + shown + "'" + (where.empty() ? "" : " in " + where) +
             (guess.empty() ? "; expected one of: " + join(valid)
                            : "; did you mean '" + guess + "'?"),
         leaf.empty() ? name : leaf);
}
// Checks that every key of the table at `index` is a setting the schema lists inside `where`
// ("" is the top level, "bindings[]" a binding, "outputs.monitors.<name>" one monitor).
void keys_among(lua_State *L, int index, const std::vector<std::string> &allowed,
                const std::string &where = "") {
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            fail(where.empty() ? "expected a named setting"
                               : "expected a named setting in " + where);
        std::string key = lua_tostring(L, -2);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            unknown("setting", key, allowed, "", where);
        }
        lua_pop(L, 1);
    }
}
void keys(lua_State *L, int index, const std::string &where) {
    std::vector<std::string> allowed;
    for (const auto *option : config_children(where))
        allowed.emplace_back(std::string(option->path).substr(where.empty() ? 0 : where.size() + 1));
    keys_among(L, index, allowed, where);
}
std::string string(lua_State *L, int index, const char *label) {
    if (lua_type(L, index) != LUA_TSTRING) {
        lua_pushvalue(L, index);
        auto kind = described(L, -1);
        lua_pop(L, 1);
        fail(std::string(label) + " must be a string, not " + kind, last_name(label));
    }
    size_t length = 0;
    const char *value = lua_tolstring(L, index, &length);
    if (length > 4096 || std::memchr(value, '\0', length))
        fail(std::string(label) + " is too long or contains a NUL byte", last_name(label));
    return {value, length};
}
std::string field(lua_State *L, const char *key) {
    lua_getfield(L, -1, key);
    auto result = string(L, -1, key);
    lua_pop(L, 1);
    return result;
}
// The string at `key`, named `label` (else `key`) in errors; nothing when it is unset.
std::optional<std::string> text(lua_State *L, const char *key, const char *label = nullptr) {
    lua_getfield(L, -1, key);
    std::optional<std::string> result;
    if (!lua_isnil(L, -1))
        result = string(L, -1, label ? label : key);
    lua_pop(L, 1);
    return result;
}
// Whether the table on top of the stack sets `key`.
bool present(lua_State *L, const char *key) {
    lua_getfield(L, -1, key);
    bool set = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return set;
}
int integer(lua_State *L, const char *key, int fallback, int min, int max) {
    lua_getfield(L, -1, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    if (!lua_isinteger(L, -1))
        wrong_type(L, key, "an integer");
    auto result = lua_tointeger(L, -1);
    if (result < min || result > max)
        out_of_range(key, static_cast<double>(result), min, max);
    lua_pop(L, 1);
    return static_cast<int>(result);
}
// The string at `key` (`label` in errors) as what it names among `names`, into `target`, which
// stays as it is while the string is unset. Another name fails with `message`, pointing at `leaf`,
// or without a message as an unknown `key`, suggesting one of the names.
template <typename T>
void named(lua_State *L, const char *key, const char *label, T &target,
           std::initializer_list<std::pair<std::string_view, T>> names,
           const char *message = nullptr, const char *leaf = "") {
    auto name = text(L, key, label);
    if (!name)
        return;
    for (const auto &[candidate, value] : names)
        if (*name == candidate) {
            target = value;
            return;
        }
    if (message)
        fail(message, leaf);
    std::vector<std::string> valid;
    for (const auto &entry : names)
        valid.emplace_back(entry.first);
    unknown(key, *name, valid, "=" + *name);
}
template <std::size_t N>
void copy_text(const std::string &value, char (&target)[N], const std::string &label) {
    if (value.size() >= N)
        fail(label + " is too long");
    std::memcpy(target, value.c_str(), value.size() + 1);
}
template <std::size_t N> void text_field(lua_State *L, const char *key, char (&target)[N]) {
    if (auto value = text(L, key))
        copy_text(*value, target, key);
}
// The boolean at `key`, named `label` (else `key`) in errors; nothing when it is unset.
std::optional<bool> optional_boolean(lua_State *L, const char *key, const char *label = nullptr) {
    lua_getfield(L, -1, key);
    std::optional<bool> result;
    if (!lua_isnil(L, -1)) {
        if (!lua_isboolean(L, -1))
            wrong_type(L, label ? label : key, "a boolean");
        result = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
    return result;
}
void boolean(lua_State *L, const char *key, const char *label, bool &target) {
    target = optional_boolean(L, key, label).value_or(target);
}
// A widget that can move: `true` for `place`, its default place, `false` for nowhere, or
// "bar" or "quick" (Quick Settings).
void placement(lua_State *L, const char *key, const char *label, WidgetPlace place,
               WidgetPlace &target) {
    lua_getfield(L, -1, key);
    if (lua_isboolean(L, -1)) {
        target = lua_toboolean(L, -1) ? place : WidgetPlace::Hidden;
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        const std::string where = lua_tostring(L, -1);
        if (where == "bar")
            target = WidgetPlace::Bar;
        else if (where == "quick")
            target = WidgetPlace::Quick;
        else
            fail(std::string(label) + " must be true, false, \"bar\" or \"quick\", not \"" + where +
                     "\"",
                 key);
    } else if (!lua_isnil(L, -1)) {
        wrong_type(L, label, "true, false, \"bar\" or \"quick\"");
    }
    lua_pop(L, 1);
}
// A boolean that may be left unset (-1) to keep a device default.
void tristate(lua_State *L, const char *key, const char *label, int &target) {
    if (auto value = optional_boolean(L, key, label))
        target = *value;
}
bool is_color(const std::string &value, bool alpha = false) {
    return (value.size() == 7 || (alpha && value.size() == 9)) && value[0] == '#' &&
           value.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos;
}
// #RRGGBB or #RRGGBBAA as premultiplied RGBA.
void premultiplied(const std::string &value, const char *label, float (&target)[4]) {
    if (!is_color(value, true))
        fail(std::string(label) + " must be #RRGGBB or #RRGGBBAA");
    float alpha = value.size() == 9 ? std::stoi(value.substr(7, 2), nullptr, 16) / 255.0F : 1.0F;
    for (size_t i = 0; i < 3; ++i)
        target[i] = std::stoi(value.substr(1 + 2 * i, 2), nullptr, 16) / 255.0F * alpha;
    target[3] = alpha;
}
// #RRGGBB or #RRGGBBAA, checked already, as straight RGBA.
void straight(const std::string &value, float (&target)[4]) {
    for (size_t i = 0; i < 4; ++i)
        target[i] = i < 3 || value.size() == 9
                        ? std::stoi(value.substr(1 + 2 * i, 2), nullptr, 16) / 255.0F
                        : 1.0F;
}
// Errors are placed in the section `path` while this lives, then where they were before.
class Named {
public:
    explicit Named(std::string path) : outer(std::exchange(current_section, std::move(path))) {}
    ~Named() { current_section = std::move(outer); }
    Named(const Named &) = delete;
    Named &operator=(const Named &) = delete;

private:
    std::string outer;
};
// The optional table `name` pushed while this lives, false when it is absent. A table there has
// its keys checked, and errors are placed in it, as `path` (else `name`); leaving pops it.
class Section {
public:
    Section(lua_State *L, const char *name, const char *path = nullptr) : L(L) {
        lua_getfield(L, -1, name);
        if (lua_isnil(L, -1))
            return;
        if (!path)
            path = name;
        named.emplace(path);
        table(L, -1, path);
        keys(L, -1, path);
    }
    ~Section() { lua_pop(L, 1); }
    Section(const Section &) = delete;
    Section &operator=(const Section &) = delete;
    explicit operator bool() const { return named.has_value(); }

private:
    lua_State *L;
    std::optional<Named> named;
};
size_t array_size(lua_State *L, int index, size_t limit) {
    table(L, index, "list");
    index = lua_absindex(L, index);
    auto size = lua_rawlen(L, index);
    if (size > limit)
        fail("list is too long");
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 ||
            static_cast<size_t>(lua_tointeger(L, -2)) > size)
            fail("lists must have consecutive integer keys starting at 1");
        ++count;
        lua_pop(L, 1);
    }
    if (count != size)
        fail("list contains holes");
    return size;
}
uint32_t modifier(const std::string &name) {
    for (auto [candidate, bit] :
         {std::pair{"Alt", SH_ALT}, {"Super", SH_LOGO}, {"Ctrl", SH_CTRL}, {"Shift", SH_SHIFT}})
        if (name == candidate)
            return bit;
    unknown("modifier", name, config_modifier_names(), "=" + name);
}
// Linux's names; most mice send side and extra from their back and forward thumb buttons
// (Hyprland's mouse:275 and mouse:276). The wheel's notches up and down count as buttons too
// (Hyprland's mouse_up and mouse_down).
uint32_t mouse_button(const std::string &name) {
    for (auto [candidate, code] :
         {std::pair<const char *, uint32_t>{"left", BTN_LEFT}, {"right", BTN_RIGHT},
          {"middle", BTN_MIDDLE}, {"side", BTN_SIDE}, {"extra", BTN_EXTRA},
          {"forward", BTN_FORWARD}, {"back", BTN_BACK}, {"wheel_up", SH_WHEEL_UP},
          {"wheel_down", SH_WHEEL_DOWN}})
        if (name == candidate)
            return code;
    unknown("button", name, config_button_names(), "=" + name);
}
// The list of strings on top of the stack, at most `limit` of them, each `label` in errors.
std::vector<std::string> strings(lua_State *L, size_t limit, const char *label) {
    auto size = array_size(L, -1, limit);
    std::vector<std::string> result;
    for (size_t i = 1; i <= size; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        result.push_back(string(L, -1, label));
        lua_pop(L, 1);
    }
    return result;
}
Command command(lua_State *L) {
    if (array_size(L, -1, 256) == 0)
        fail("command must include an executable");
    auto result = strings(L, 256, "command argument");
    if (result.front().empty())
        fail("command executable is empty");
    return result;
}
// The dimming a `display_off` of so many milliseconds brings when `dim` is unset: 30 seconds
// before, halfway under a minute, none without it.
int idle_dim_before(int display_off) {
    return display_off > 0 ? display_off - std::min(30000, display_off / 2) : 0;
}
// One table of `idle` steps, in seconds (0 for never), into milliseconds; steps it leaves out
// stay as they are.
void read_idle_steps(lua_State *L, sh_idle_steps &steps) {
    constexpr int day = 24 * 60 * 60;
    bool dim_set = present(L, "dim"), off_set = present(L, "display_off");
    steps.display_off = 1000 * integer(L, "display_off", steps.display_off / 1000, 0, day);
    steps.lock = 1000 * integer(L, "lock", steps.lock / 1000, 0, day);
    steps.suspend = 1000 * integer(L, "suspend", steps.suspend / 1000, 0, day);
    if (dim_set)
        steps.dim = 1000 * integer(L, "dim", 0, 0, day);
    else if (off_set)
        steps.dim = idle_dim_before(steps.display_off);
}
// `idle`, and `idle.battery`, which starts from it.
void read_idle(lua_State *L, sh_settings &settings) {
    if (Section in{L, "idle"}) {
        read_idle_steps(L, settings.idle);
        settings.idle_battery = settings.idle;
        if (Section battery{L, "battery", "idle.battery"})
            read_idle_steps(L, settings.idle_battery);
    }
}
// A switch binding, { switch = "lid", state = "close", action = ... }: no key, button or mods.
void read_switch_binding(lua_State *L, Binding &binding) {
    for (const char *other : {"key", "button", "mods", "app_id", "desktop"})
        if (present(L, other))
            fail(std::string(other) + " is not valid with a switch", other);
    auto name = field(L, "switch"), state = field(L, "state");
    if (name == "lid") {
        if (state != "close" && state != "open")
            fail("the lid's state is \"close\" or \"open\", not '" + state + "'", "state");
        binding.switch_type = SH_SWITCH_LID;
        binding.switch_on = state == "close";
    } else if (name == "tablet") {
        if (state != "on" && state != "off")
            fail("tablet mode's state is \"on\" or \"off\", not '" + state + "'", "state");
        binding.switch_type = SH_SWITCH_TABLET;
        binding.switch_on = state == "on";
    } else {
        unknown("switch", name, {"lid", "tablet"}, "switch");
    }
}
// shell.tiling_bar: false, true, or the bar's table on top of the stack.
void read_tiling_bar(lua_State *L, TilingBar &bar) {
    lua_getfield(L, -1, "tiling_bar");
    if (lua_isboolean(L, -1) || lua_isnil(L, -1)) {
        if (lua_isboolean(L, -1))
            bar.enabled = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1))
        wrong_type(L, "tiling_bar", "true, false or a table");
    keys(L, -1, "shell.tiling_bar");
    Named in("shell.tiling_bar");
    boolean(L, "enabled", "shell.tiling_bar.enabled", bar.enabled);
    named(L, "position", nullptr, bar.top, {{"top", true}, {"bottom", false}},
          "shell.tiling_bar.position must be \"top\" or \"bottom\"", "position");
    bar.height = integer(L, "height", bar.height, 20, 100);
    // One number for every side, or { top, right, bottom, left } by name, the sides left out 0;
    // unset, away from the monitor's edges but not towards the windows.
    lua_getfield(L, -1, "margin");
    if (lua_isinteger(L, -1)) {
        auto margin = lua_tointeger(L, -1);
        if (margin < 0 || margin > 200)
            out_of_range("margin", static_cast<double>(margin), 0, 200);
        for (auto &side : bar.margin)
            side = static_cast<int>(margin);
    } else if (!lua_isnil(L, -1)) {
        table(L, -1, "shell.tiling_bar.margin");
        keys(L, -1, "shell.tiling_bar.margin");
        Named margin("shell.tiling_bar.margin");
        int index = 0;
        for (const char *side : {"top", "right", "bottom", "left"})
            bar.margin[index++] = integer(L, side, 0, 0, 200);
    } else if (!bar.top) {
        std::swap(bar.margin[0], bar.margin[2]);
    }
    lua_pop(L, 1);
    bar.radius = integer(L, "radius", bar.radius, 0, 50);
    named(L, "style", nullptr, bar.style,
          {{"bar", TilingBarStyle::Bar}, {"islands", TilingBarStyle::Islands},
           {"pills", TilingBarStyle::Pills}},
          "shell.tiling_bar.style must be \"bar\", \"islands\" or \"pills\"", "style");
    for (auto [key, target] : {std::pair{"color", &bar.color},
                               {"text_color", &bar.text_color},
                               {"accent", &bar.accent},
                               {"border_color", &bar.border_color}}) {
        if (auto value = text(L, key)) {
            if (!is_color(*value, true))
                fail("shell.tiling_bar." + std::string(key) + " must be #RRGGBB or #RRGGBBAA", key);
            *target = *value;
        }
    }
    bar.border_width = integer(L, "border_width", bar.border_width, 0, 10);
    if (auto font = text(L, "font"))
        bar.font = *font;
    bar.font_size = integer(L, "font_size", bar.font_size, 6, 48);
    bar.spacing = integer(L, "spacing", bar.spacing, 0, 40);
    named(L, "workspace_style", nullptr, bar.workspace_dots, {{"numbers", false}, {"dots", true}},
          "shell.tiling_bar.workspace_style must be \"numbers\" or \"dots\"", "workspace_style");
    if (auto format = text(L, "clock_format")) {
        if (format->empty() || format->size() > 64)
            fail("shell.tiling_bar.clock_format must have 1 to 64 bytes", "clock_format");
        bar.clock_format = *format;
    }
    // A table of them lays out the whole bar: a group it leaves out is empty.
    if (Section modules{L, "modules", "shell.tiling_bar.modules"}) {
        const auto &known = tiling_bar_modules();
        for (auto [key, target] :
             {std::pair{"left", &bar.left}, {"center", &bar.center}, {"right", &bar.right}}) {
            target->clear();
            lua_getfield(L, -1, key);
            if (!lua_isnil(L, -1)) {
                auto label = "shell.tiling_bar.modules." + std::string(key);
                *target = strings(L, 16, label.c_str());
                for (const auto &module : *target)
                    if (std::find(known.begin(), known.end(), module) == known.end())
                        unknown("module", module, known, key, label);
            }
            lua_pop(L, 1);
        }
        // One place along the bar for each.
        std::vector<std::string> seen;
        for (const auto *group : {&bar.left, &bar.center, &bar.right})
            for (const auto &module : *group) {
                if (std::find(seen.begin(), seen.end(), module) != seen.end())
                    fail("shell.tiling_bar.modules names '" + module + "' twice", module);
                seen.push_back(module);
            }
    }
    lua_pop(L, 1);
}
// shell.desktop_clock: true, false, or the clock's table, on top of the stack.
void read_desktop_clock(lua_State *L, DesktopClockConfig &clock) {
    lua_getfield(L, -1, "desktop_clock");
    if (lua_isboolean(L, -1) || lua_isnil(L, -1)) {
        if (lua_isboolean(L, -1))
            clock.enabled = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1))
        wrong_type(L, "desktop_clock", "true, false or a table");
    keys(L, -1, "shell.desktop_clock");
    Named in("shell.desktop_clock");
    boolean(L, "enabled", "shell.desktop_clock.enabled", clock.enabled);
    if (auto position = text(L, "position")) {
        static const std::vector<std::string> known{"center",   "top",       "bottom",
                                                    "left",     "right",     "top-left",
                                                    "top-right", "bottom-left", "bottom-right"};
        if (std::find(known.begin(), known.end(), *position) == known.end())
            unknown("position", *position, known, "=" + *position);
        clock.position = *position;
    }
    if (auto format = text(L, "time_format")) {
        if (format->empty() || format->size() > 64)
            fail("shell.desktop_clock.time_format must have 1 to 64 bytes", "time_format");
        clock.time_format = *format;
    }
    if (auto format = text(L, "date_format")) {
        if (format->size() > 64)
            fail("shell.desktop_clock.date_format must have at most 64 bytes", "date_format");
        clock.date_format = *format;
    }
    clock.size = integer(L, "size", clock.size, 24, 320);
    if (auto color = text(L, "color")) {
        if (!is_color(*color, true))
            fail("shell.desktop_clock.color must be #RRGGBB or #RRGGBBAA", "color");
        clock.color = *color;
    }
    if (auto font = text(L, "font"))
        clock.font = *font;
    // false, "en" or "ja".
    lua_getfield(L, -1, "greeting");
    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
        clock.greeting.clear();
    } else if (!lua_isnil(L, -1)) {
        auto greeting = lua_type(L, -1) == LUA_TSTRING ? std::string(lua_tostring(L, -1)) : "";
        if (greeting != "en" && greeting != "ja")
            fail("shell.desktop_clock.greeting must be false, \"en\" or \"ja\"", "greeting");
        clock.greeting = greeting;
    }
    lua_pop(L, 1);
    lua_pop(L, 1);
}
// The edges a bar can be along, as shell.panel_positions names them.
constexpr std::pair<std::string_view, PanelEdge> panel_edges[] = {
    {"top", PanelEdge::Top}, {"bottom", PanelEdge::Bottom}, {"left", PanelEdge::Left},
    {"right", PanelEdge::Right}};
double number(lua_State *L, const char *key, double fallback, double min, double max);
void read_shell(lua_State *L, ShellConfig &shell) {
    Section section(L, "shell");
    if (!section)
        return;
    boolean(L, "enabled", "shell.enabled", shell.enabled);
    named(L, "style", nullptr, shell.macos_style, {{"taskbar", false}, {"macos", true}},
          "style must be \"taskbar\" or \"macos\"");
    shell.panel_height = integer(L, "panel_height", 52, 24, 100);
    named(L, "panel_position", nullptr, shell.panel_edge,
          {{"top", PanelEdge::Top}, {"bottom", PanelEdge::Bottom}, {"left", PanelEdge::Left},
           {"right", PanelEdge::Right}},
          "panel_position must be \"top\", \"bottom\", \"left\" or \"right\"");
    boolean(L, "panel_per_monitor", "shell.panel_per_monitor", shell.panel_per_monitor);
    // { ["DP-1"] = "left", ... }: an edge for each monitor named.
    lua_getfield(L, -1, "panel_positions");
    if (!lua_isnil(L, -1)) {
        table(L, -1, "shell.panel_positions");
        Named in("shell.panel_positions");
        const int index = lua_absindex(L, -1);
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                fail("shell.panel_positions is keyed by output name, e.g. [\"DP-1\"] = \"left\"");
            const std::string name = lua_tostring(L, -2);
            if (name.empty())
                fail("output name is empty");
            const auto edge = string(L, -1, ("shell.panel_positions[\"" + name + "\"]").c_str());
            auto found = std::find_if(std::begin(panel_edges), std::end(panel_edges),
                                      [&](const auto &entry) { return entry.first == edge; });
            if (found == std::end(panel_edges))
                fail("shell.panel_positions[\"" + name +
                         "\"] must be \"top\", \"bottom\", \"left\" or \"right\"",
                     "=" + edge);
            shell.panel_positions[name] = found->second;
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    boolean(L, "panel_draggable", "shell.panel_draggable", shell.panel_draggable);
    // true, false or "maximized".
    lua_getfield(L, -1, "panel_dock");
    if (lua_isboolean(L, -1)) {
        shell.panel_dock = lua_toboolean(L, -1) ? ShellConfig::PanelDock::Always
                                                : ShellConfig::PanelDock::Never;
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        const std::string mode = lua_tostring(L, -1);
        if (mode != "maximized")
            fail("shell.panel_dock must be true, false or \"maximized\", not \"" + mode + "\"",
                 "panel_dock");
        shell.panel_dock = ShellConfig::PanelDock::Maximized;
    } else if (!lua_isnil(L, -1)) {
        wrong_type(L, "shell.panel_dock", "true, false or \"maximized\"");
    }
    lua_pop(L, 1);
    boolean(L, "mirror_menus", "shell.mirror_menus", shell.mirror_menus);
    // One number for every side, or { top, right, bottom, left } by name.
    lua_getfield(L, -1, "panel_margin");
    if (lua_isinteger(L, -1)) {
        auto margin = lua_tointeger(L, -1);
        if (margin < 0 || margin > 200)
            fail("panel_margin is out of range");
        for (auto &side : shell.panel_margin)
            side = static_cast<int>(margin);
    } else if (!lua_isnil(L, -1)) {
        table(L, -1, "panel_margin");
        keys(L, -1, "shell.panel_margin");
        Named in("shell.panel_margin");
        int index = 0;
        for (const char *side : {"top", "right", "bottom", "left"})
            shell.panel_margin[index++] = integer(L, side, 0, 0, 200);
    }
    lua_pop(L, 1);
    shell.panel_radius = integer(L, "panel_radius", 0, 0, 50);
    named(L, "frame", nullptr, shell.frame_windows, {{"shell", false}, {"windows", true}},
          "frame must be \"shell\" or \"windows\"");
    if (Section in{L, "blur", "shell.blur"}) {
        auto &blur = shell.blur;
        boolean(L, "enabled", "shell.blur.enabled", blur.enabled);
        blur.radius = integer(L, "radius", blur.radius, 2, 200);
        blur.brightness = number(L, "brightness", blur.brightness, 0, 2);
        blur.saturation = number(L, "saturation", blur.saturation, 0, 2);
    }
    if (auto font = text(L, "font"))
        shell.font = *font;
    shell.font_size = integer(L, "font_size", 12, 6, 48);
    named(L, "renderer", nullptr, shell.software_renderer, {{"software", true}, {"gpu", false}},
          "renderer must be \"software\" or \"gpu\"");
    boolean(L, "icons_only", "shell.icons_only", shell.icons_only);
    boolean(L, "group_windows", "shell.group_windows", shell.group_windows);
    named(L, "taskbar_buttons", nullptr, shell.taskbar_buttons,
          {{"all", ShellConfig::TaskbarButtons::All},
           {"main_and_own", ShellConfig::TaskbarButtons::MainAndOwn},
           {"own", ShellConfig::TaskbarButtons::Own}},
          "taskbar_buttons must be \"all\", \"main_and_own\" or \"own\"");
    boolean(L, "polkit_agent", "shell.polkit_agent", shell.polkit_agent);
    shell.workspaces_shown = integer(L, "workspaces_shown", 0, 0, 10);
    if (Section in{L, "thumbnails", "shell.thumbnails"}) {
        auto &thumbnails = shell.thumbnails;
        boolean(L, "enabled", "shell.thumbnails.enabled", thumbnails.enabled);
        thumbnails.delay = integer(L, "delay", thumbnails.delay, 0, 2000);
        thumbnails.size = integer(L, "size", thumbnails.size, 120, 480);
        boolean(L, "live", "shell.thumbnails.live", thumbnails.live);
        boolean(L, "workspaces", "shell.thumbnails.workspaces", thumbnails.workspaces);
    }
    if (Section in{L, "search", "shell.search"}) {
        auto &search = shell.search;
        boolean(L, "files", "shell.search.files", search.files);
        lua_getfield(L, -1, "directories");
        if (!lua_isnil(L, -1))
            for (auto &folder : strings(L, 32, "shell.search.directories")) {
                if (!folder.starts_with('/') && !folder.starts_with("~/") && folder != "~")
                    fail("shell.search.directories must be absolute or start with ~/");
                search.directories.push_back(std::move(folder));
            }
        lua_pop(L, 1);
        search.depth = integer(L, "depth", search.depth, 1, 10);
        search.max_files = integer(L, "max_files", search.max_files, 100, 200000);
        // An engine's address, or false for none.
        lua_getfield(L, -1, "web");
        if (lua_isboolean(L, -1)) {
            if (lua_toboolean(L, -1))
                fail("shell.search.web must be an address with %s or false, not true", "web");
            search.web.clear();
        } else if (!lua_isnil(L, -1)) {
            search.web = string(L, -1, "shell.search.web");
            if ((!search.web.starts_with("https://") && !search.web.starts_with("http://")) ||
                search.web.find("%s") == std::string::npos)
                fail("shell.search.web must be an http or https address with %s where the words go",
                     "web");
        }
        lua_pop(L, 1);
    }
    if (Section in{L, "clipboard", "shell.clipboard"}) {
        auto &clipboard = shell.clipboard;
        boolean(L, "enabled", "shell.clipboard.enabled", clipboard.enabled);
        clipboard.max_entries = integer(L, "max_entries", clipboard.max_entries, 1, 500);
        boolean(L, "images", "shell.clipboard.images", clipboard.images);
        boolean(L, "persist", "shell.clipboard.persist", clipboard.persist);
    }
    if (Section in{L, "widgets", "shell.widgets"}) {
        for (auto [key, target] : {std::pair{"workspaces", &shell.widgets.workspaces},
                                   {"clock", &shell.widgets.clock},
                                   {"calendar", &shell.widgets.calendar},
                                   {"keyboard_layout", &shell.widgets.keyboard_layout},
                                   {"power", &shell.widgets.power},
                                   {"tray", &shell.widgets.tray},
                                   {"media", &shell.widgets.media},
                                   {"power_mode", &shell.widgets.power_mode},
                                   {"bluetooth", &shell.widgets.bluetooth}})
            boolean(L, key, (std::string("shell.widgets.") + key).c_str(), *target);
        // Those that can move, and where `true` puts them.
        struct Movable {
            const char *key;
            WidgetPlace *target;
            WidgetPlace place;
        };
        for (auto [key, target, place] : {Movable{"battery", &shell.widgets.battery, WidgetPlace::Quick},
                                          {"network", &shell.widgets.network, WidgetPlace::Quick},
                                          {"volume", &shell.widgets.volume, WidgetPlace::Quick},
                                          {"tiling", &shell.widgets.tiling, WidgetPlace::Bar},
                                          {"profiles", &shell.widgets.profiles, WidgetPlace::Quick},
                                          {"wallpapers", &shell.widgets.wallpapers, WidgetPlace::Bar},
                                          {"notifications", &shell.widgets.notifications, WidgetPlace::Quick}})
            placement(L, key, (std::string("shell.widgets.") + key).c_str(), place, *target);
    }
    read_tiling_bar(L, shell.tiling_bar);
    for (auto [key, target] : {std::pair{"accent", &shell.accent},
                               {"panel_color", &shell.panel_color},
                               {"text_color", &shell.text_color},
                               {"wallpaper", &shell.wallpaper},
                               {"start_icon", &shell.start_icon}}) {
        if (auto value = text(L, key)) {
            *target = *value;
            if (target != &shell.wallpaper && target != &shell.start_icon &&
                !is_color(*target, true))
                fail(std::string(key) + " must be #RRGGBB or #RRGGBBAA");
        }
    }
    // { ["DP-1"] = "path", ... }: a wallpaper for each monitor named.
    lua_getfield(L, -1, "monitor_wallpapers");
    if (!lua_isnil(L, -1)) {
        table(L, -1, "shell.monitor_wallpapers");
        Named in("shell.monitor_wallpapers");
        const int index = lua_absindex(L, -1);
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                fail("shell.monitor_wallpapers is keyed by output name, e.g. "
                     "[\"DP-1\"] = \"sky.jpg\"");
            const std::string name = lua_tostring(L, -2);
            if (name.empty() || name.find_first_of("\t\n") != std::string::npos)
                fail("output name is empty or has a tab or a newline");
            shell.monitor_wallpapers[name] =
                string(L, -1, ("shell.monitor_wallpapers[\"" + name + "\"]").c_str());
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    named(L, "wallpaper_mode", "shell.wallpaper_mode", shell.wallpaper_mode,
          {{"fill", WallpaperMode::Fill}, {"fit", WallpaperMode::Fit},
           {"center", WallpaperMode::Center}, {"tile", WallpaperMode::Tile},
           {"stretch", WallpaperMode::Stretch}});
    read_desktop_clock(L, shell.desktop_clock);
    if (Section in{L, "wallpaper_cycle", "shell.wallpaper_cycle"}) {
        auto &cycle = shell.wallpaper_cycle;
        cycle.interval = integer(L, "interval", cycle.interval, 0, 1440);
        if (auto folder = text(L, "folder", "shell.wallpaper_cycle.folder"))
            cycle.folder = *folder;
        named(L, "order", "shell.wallpaper_cycle.order", cycle.shuffle,
              {{"shuffle", true}, {"name", false}});
        boolean(L, "per_monitor", "shell.wallpaper_cycle.per_monitor", cycle.per_monitor);
    }
    if (auto folder = text(L, "wallpapers", "shell.wallpapers")) {
        shell.wallpapers = *folder;
        if (!shell.wallpapers.starts_with('/') && !shell.wallpapers.starts_with("~/"))
            fail("shell.wallpapers must be absolute or start with ~/");
    }
    lua_getfield(L, -1, "launchers");
    if (!lua_isnil(L, -1)) {
        auto size = array_size(L, -1, 64);
        for (size_t i = 1; i <= size; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            table(L, -1, "launcher");
            keys(L, -1, "shell.launchers[]");
            Launcher launcher;
            launcher.name = field(L, "name");
            if (launcher.name.empty() || launcher.name.size() > 128)
                fail("launcher name must have 1 to 128 bytes");
            launcher.icon = text(L, "icon").value_or("application-x-executable");
            lua_getfield(L, -1, "command");
            launcher.command = command(L);
            lua_pop(L, 1);
            shell.launchers.push_back(std::move(launcher));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}
constexpr const char *tile_layout_names[] = {"dwindle", "master", "spiral", "monocle", "scroll"};
double number(lua_State *L, const char *key, double fallback, double min, double max) {
    lua_getfield(L, -1, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return fallback;
    }
    if (lua_type(L, -1) != LUA_TNUMBER)
        wrong_type(L, key, "a number");
    auto result = lua_tonumber(L, -1);
    if (!(result >= min && result <= max))
        out_of_range(key, result, min, max);
    lua_pop(L, 1);
    return result;
}
// animations.<kind> names, in enum sh_anim_kind order.
constexpr const char *animation_kinds[SH_ANIM_KINDS] = {"open",      "close",      "move",
                                                        "workspace", "fullscreen", "focus",
                                                        "minimize",  "cube"};
void read_curve(lua_State *L, const char *key, const std::string &label, sh_curve &target,
                bool &given) {
    if (auto name = text(L, key, label.c_str())) {
        if (!sh_curve_parse(name->c_str(), &target))
            fail(label + " must be \"linear\", \"ease-in\", \"ease-out\", \"ease-in-out\", "
                         "\"ease-out-quint\", \"overshoot\", \"spring\", or "
                         "\"bezier(x1, y1, x2, y2)\", not \"" + *name + "\"",
                 key);
        given = true;
    }
}
// The `animations` table is on top of the stack: a base duration and curve, a speed, and per-kind
// tables that override the base.
void read_animations(lua_State *L, sh_settings &settings) {
    int base = integer(L, "duration", 120, 10, 1000);
    settings.animation_speed = static_cast<float>(number(L, "speed", 1, 0.1, 10));
    settings.animation_late_ms = integer(L, "late_frame_ms", 80, 0, 1000);
    sh_curve curve{};
    bool curved = false;
    read_curve(L, "curve", "animations.curve", curve, curved);
    for (auto &style : settings.animation_styles) {
        style.duration = base;
        if (curved)
            style.curve = curve;
    }
    // A window travels to its taskbar button and back, further than the others go, and the
    // scrolling layout's cube turns a quarter, which takes a moment to see.
    settings.animation_styles[SH_ANIM_MINIMIZE].duration = std::min(1000, base * 5 / 2);
    settings.animation_styles[SH_ANIM_CUBE].duration = std::min(1000, base * 5 / 2);
    for (int kind = 0; kind < SH_ANIM_KINDS; ++kind) {
        std::string path = std::string("animations.") + animation_kinds[kind];
        Section in(L, animation_kinds[kind], path.c_str());
        if (!in)
            continue;
        auto &style = settings.animation_styles[kind];
        style.duration = integer(L, "duration", style.duration, 0, 1000);
        bool own = false;
        read_curve(L, "curve", path + ".curve", style.curve, own);
        if (kind == SH_ANIM_WORKSPACE)
            settings.animation_slide = static_cast<float>(number(L, "distance", 0.08, 0, 1));
    }
}
// layout.scroll, with its table on top of the stack.
void read_scroll(lua_State *L, sh_settings &settings) {
    named(L, "follow", "layout.scroll.follow", settings.scroll_follow,
          {{"center", SH_SCROLL_FOLLOW_CENTER},
           {"edge", SH_SCROLL_FOLLOW_EDGE},
           {"never", SH_SCROLL_FOLLOW_NEVER}},
          "layout.scroll.follow must be \"center\", \"edge\", or \"never\"");
    settings.scroll_width = static_cast<float>(number(L, "width", settings.scroll_width, 0.1, 1));
    settings.scroll_step = static_cast<float>(number(L, "step", settings.scroll_step, 0.01, 0.5));
    boolean(L, "fill_lone", "layout.scroll.fill_lone", settings.scroll_fill_lone);
    boolean(L, "cube", "layout.scroll.cube", settings.scroll_cube);
    lua_getfield(L, -1, "presets");
    if (!lua_isnil(L, -1)) {
        auto size = array_size(L, -1, std::size(settings.scroll_presets));
        if (size == 0)
            fail("layout.scroll.presets is empty");
        for (size_t i = 1; i <= size; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            if (lua_type(L, -1) != LUA_TNUMBER)
                wrong_type(L, "layout.scroll.presets", "a list of numbers");
            double value = lua_tonumber(L, -1);
            if (!(value >= 0.1 && value <= 1))
                out_of_range("layout.scroll.presets", value, 0.1, 1);
            settings.scroll_presets[i - 1] = static_cast<float>(value);
            lua_pop(L, 1);
        }
        settings.scroll_preset_count = static_cast<int>(size);
    }
    lua_pop(L, 1);
}
int tile_layout_index(const std::string &name) {
    for (int i = 0; i < SH_LAYOUT_COUNT; ++i)
        if (name == tile_layout_names[i])
            return i;
    fail("layout.tile_layout must be \"dwindle\", \"master\", \"spiral\", \"monocle\", or "
         "\"scroll\"");
}
// layout.outputs, with the layout table on top of the stack.
void read_output_layouts(lua_State *L, sh_settings &settings) {
    lua_getfield(L, -1, "outputs");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    table(L, -1, "layout.outputs");
    int index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            fail("layout.outputs is keyed by output name, e.g. [\"DP-1\"] = { ... }");
        if (settings.output_layout_count == static_cast<int>(std::size(settings.output_layouts)))
            fail("layout.outputs has too many entries");
        auto &entry = settings.output_layouts[settings.output_layout_count++];
        auto name = string(L, -2, "output name");
        if (name.empty())
            fail("output name is empty");
        copy_text(name, entry.name, "output name");
        Named in("layout.outputs[\"" + name + "\"]");
        table(L, -1, "layout.outputs.<name>");
        keys(L, -1, "layout.outputs.<name>");
        auto layout = text(L, "tile_layout");
        entry.tile_layout = layout ? tile_layout_index(*layout) : -1;
        entry.master_ratio = static_cast<float>(number(L, "master_ratio", 0, 0.1, 0.9));
        entry.master_count = integer(L, "master_count", 0, 1, 8);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}
// "WIDTHxHEIGHT" or "WIDTHxHEIGHT@HZ", where HZ may have decimals.
void parse_mode(const std::string &mode, sh_monitor &monitor) {
    int width = 0, height = 0, used = 0;
    double hz = 0;
    auto *text = mode.c_str();
    bool valid = std::sscanf(text, "%5dx%5d%n", &width, &height, &used) == 2;
    if (valid && text[used] == '@') {
        int more = 0;
        valid = std::sscanf(text + used + 1, "%lf%n", &hz, &more) == 1 && hz >= 1 && hz <= 1000;
        used += 1 + more;
    }
    if (!valid || text[used] != '\0' || width < 1 || height < 1 || width > 16384 || height > 16384)
        fail("mode must be WIDTHxHEIGHT or WIDTHxHEIGHT@HZ, e.g. 2560x1440@144");
    monitor.width = width;
    monitor.height = height;
    monitor.refresh = static_cast<int>(hz * 1000 + 0.5);
}
void read_monitors(lua_State *L, sh_settings &settings) {
    lua_getfield(L, -1, "monitors");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    table(L, -1, "outputs.monitors");
    int index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            fail("outputs.monitors is keyed by output name, e.g. [\"DP-1\"] = { ... }");
        if (settings.monitor_count == static_cast<int>(std::size(settings.monitors)))
            fail("outputs.monitors has too many entries");
        auto &monitor = settings.monitors[settings.monitor_count++];
        auto name = string(L, -2, "output name");
        if (name.empty())
            fail("output name is empty");
        copy_text(name, monitor.name, "output name");
        Named in("outputs.monitors[\"" + name + "\"]");
        table(L, -1, "monitor settings");
        keys(L, -1, "outputs.monitors.<name>");
        monitor.enabled = true;
        boolean(L, "enabled", "enabled", monitor.enabled);
        monitor.tiling = -1;
        tristate(L, "tiling", "tiling", monitor.tiling);
        if (auto mode = text(L, "mode"))
            parse_mode(*mode, monitor);
        monitor.scale = static_cast<float>(number(L, "scale", 0, 0.25, 10));
        monitor.transform = integer(L, "transform", 0, 0, 7);
        boolean(L, "vrr", "vrr", monitor.vrr);
        boolean(L, "hdr", "hdr", monitor.hdr);
        monitor.bit_depth = integer(L, "bit_depth", 8, 8, 10);
        if (monitor.bit_depth != 8 && monitor.bit_depth != 10)
            fail("bit_depth must be 8 or 10");
        if (auto source = text(L, "mirror")) {
            if (!valid_output_target(*source))
                fail("mirror must be a monitor's connector name or \"desc:\" and the start of "
                     "its description");
            if (*source == name)
                fail(name + " cannot mirror itself");
            copy_text(*source, monitor.mirror, "mirror");
        }
        lua_getfield(L, -1, "position");
        if (!lua_isnil(L, -1)) {
            table(L, -1, "position");
            keys(L, -1, "outputs.monitors.<name>.position");
            monitor.positioned = true;
            constexpr int unset = 1 << 30;
            monitor.x = integer(L, "x", unset, -65536, 65536);
            monitor.y = integer(L, "y", unset, -65536, 65536);
            if (monitor.x == unset || monitor.y == unset)
                fail("position needs both x and y");
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
}
// A window rule's or a binding's pattern, compiled to be matched by `pattern_found`: as
// `[\s\S]*?(?:PATTERN)` from the start of the text alone, by libstdc++'s breadth-first matcher,
// in time that grows linearly with the text. Searched for as written, it would be tried from
// each place in turn by its backtracking matcher, which for some patterns takes time exponential
// in the text and recurses once a character, deep enough on a long title to overflow the stack.
// A pattern with a back-reference, which the breadth-first matcher cannot follow, keeps the
// backtracking one.
std::regex compile_pattern(const std::string &source) {
    // Checked as written first: one that is no pattern alone, such as `a)|(b`, would be one
    // wrapped.
    (void)std::regex(source, std::regex::ECMAScript);
    const auto anywhere = "[\\s\\S]*?(?:" + source + ")";
#ifdef __GLIBCXX__
    try {
        return std::regex(anywhere, std::regex::ECMAScript | std::regex_constants::__polynomial);
    } catch (const std::regex_error &) {
    }
#endif
    return std::regex(anywhere, std::regex::ECMAScript);
}
std::optional<std::regex> pattern_field(lua_State *L, const char *key, std::string &source) {
    std::optional<std::regex> result;
    if (auto value = text(L, key)) {
        source = *value;
        try {
            result = compile_pattern(source);
        } catch (const std::regex_error &) {
            fail(std::string(key) + " '" + source + "' is not a valid regular expression");
        }
    }
    return result;
}
// `{ A, B }` or `{ first = A, second = B }`, both integers in [min, max].
std::pair<int, int> integer_pair(lua_State *L, const char *label, const char *first,
                                 const char *second, int min, int max) {
    table(L, -1, label);
    auto message = std::string(label) + " must be { " + first + ", " + second + " }";
    std::pair<int, int> result;
    if (lua_rawlen(L, -1) > 0) {
        if (array_size(L, -1, 2) != 2)
            fail(message);
        int *targets[] = {&result.first, &result.second};
        for (lua_Integer i = 1; i <= 2; ++i) {
            lua_rawgeti(L, -1, i);
            if (!lua_isinteger(L, -1))
                fail(message);
            auto value = lua_tointeger(L, -1);
            if (value < min || value > max)
                fail(std::string(label) + " is out of range");
            *targets[i - 1] = static_cast<int>(value);
            lua_pop(L, 1);
        }
        return result;
    }
    keys_among(L, -1, {first, second});
    constexpr int unset = 1 << 30;
    result = {integer(L, first, unset, min, max), integer(L, second, unset, min, max)};
    if (result.first == unset || result.second == unset)
        fail(message);
    return result;
}
void border_colors(lua_State *L, const char *key, float (&first)[4], sh_gradient &gradient);
// A rule's border colour, `border_color` or `border_inactive_color`, as windows' are given.
std::optional<sh_gradient> rule_border_color(lua_State *L, const char *key) {
    sh_gradient gradient{};
    float first[4];
    border_colors(L, key, first, gradient);
    return gradient.count ? std::optional(gradient) : std::nullopt;
}
WindowRule window_rule(lua_State *L, int workspaces) {
    WindowRule rule;
    rule.pattern = pattern_field(L, "app_id", rule.app_id);
    rule.title_pattern = pattern_field(L, "title", rule.title);
    if (!rule.pattern && !rule.title_pattern)
        fail("a window rule needs an app_id or a title");
    bool has_opacity = present(L, "opacity") || present(L, "inactive_opacity");
    rule.opacity = static_cast<float>(number(L, "opacity", 1, 0.05, 1));
    rule.inactive_opacity =
        static_cast<float>(number(L, "inactive_opacity", rule.opacity, 0.05, 1));
    auto &actions = rule.actions;
    actions.floating = optional_boolean(L, "floating");
    actions.fullscreen = optional_boolean(L, "fullscreen");
    actions.maximize = optional_boolean(L, "maximize");
    actions.focus = optional_boolean(L, "focus");
    actions.sticky = optional_boolean(L, "sticky");
    actions.shortcuts_inhibit = optional_boolean(L, "shortcuts_inhibit");
    actions.above = optional_boolean(L, "above");
    actions.allow_tearing = optional_boolean(L, "allow_tearing");
    actions.blur = optional_boolean(L, "blur");
    actions.scratchpad = optional_boolean(L, "scratchpad");
    actions.shadow = optional_boolean(L, "shadow");
    actions.border = optional_boolean(L, "border");
    if (present(L, "corner_radius"))
        actions.corner_radius = integer(L, "corner_radius", 0, 0, 40);
    actions.border_color = rule_border_color(L, "border_color");
    actions.border_inactive_color = rule_border_color(L, "border_inactive_color");
    if (present(L, "idle_inhibit")) {
        sh_idle_rule idle = SH_IDLE_RULE_UNSET;
        named(L, "idle_inhibit", qualified("idle_inhibit").c_str(), idle,
              {{"none", SH_IDLE_RULE_NONE},
               {"always", SH_IDLE_RULE_ALWAYS},
               {"visible", SH_IDLE_RULE_VISIBLE},
               {"focus", SH_IDLE_RULE_FOCUS},
               {"fullscreen", SH_IDLE_RULE_FULLSCREEN}});
        actions.idle_inhibit = idle;
    }
    if (present(L, "workspace"))
        actions.workspace = integer(L, "workspace", 0, 1, workspaces);
    if (auto name = text(L, "output")) {
        if (name->empty() || *name == "desc:")
            fail("window rule output is empty");
        if (name->size() >= sizeof(sh_window_rule{}.output))
            fail("window rule output is too long");
        actions.output = name;
    }
    lua_getfield(L, -1, "size");
    if (!lua_isnil(L, -1))
        actions.size = integer_pair(L, "size", "width", "height", 1, 16384);
    lua_pop(L, 1);
    lua_getfield(L, -1, "position");
    if (lua_type(L, -1) == LUA_TSTRING) {
        if (string(L, -1, "position") != "center")
            fail("position must be \"center\" or { x, y }");
        actions.position = WindowActions::Position::Center;
    } else if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1))
            fail("position must be \"center\" or { x, y }");
        std::tie(actions.x, actions.y) = integer_pair(L, "position", "x", "y", -16384, 16384);
        actions.position = WindowActions::Position::At;
    }
    lua_pop(L, 1);
    rule.dynamic = optional_boolean(L, "dynamic").value_or(false);
    // A rule only for actions leaves opacity to the rules and defaults after it.
    rule.sets_opacity = has_opacity || actions.empty();
    return rule;
}
// `windows.swallow = { enabled, terminals, exceptions }`: a list of app_ids replaces its default.
void swallow_list(lua_State *L, const char *key, char (*names)[64], int &count) {
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
        std::string label = std::string("windows.swallow.") + key;
        count = 0;
        for (const auto &name : strings(L, 32, label.c_str()))
            copy_text(name, names[count++], label + " entry");
    }
    lua_pop(L, 1);
}
void read_swallow(lua_State *L, Config &config) {
    if (Section in{L, "swallow", "windows.swallow"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "windows.swallow.enabled", settings.swallow);
        swallow_list(L, "terminals", settings.swallow_terminals, settings.swallow_terminal_count);
        swallow_list(L, "exceptions", settings.swallow_exceptions,
                     settings.swallow_exception_count);
    }
}
// `windows.magnet = { enabled, distance, guides, guide_color, bypass }`.
void read_magnet(lua_State *L, Config &config) {
    if (Section in{L, "magnet", "windows.magnet"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "windows.magnet.enabled", settings.magnet);
        settings.magnet_distance = integer(L, "distance", settings.magnet_distance, 0, 200);
        boolean(L, "guides", "windows.magnet.guides", settings.magnet_guides);
        if (auto color = text(L, "guide_color"))
            premultiplied(*color, "guide_color", settings.magnet_guide_color);
        if (auto name = text(L, "bypass", "windows.magnet.bypass"))
            settings.magnet_bypass = *name == "none" ? 0 : modifier(*name);
    }
}
// `windows.snap = { enabled, distance, corners, preview, color, assist }`. The preview's colour follows
// the focused window's border, a quarter opaque, unless it is given.
void read_snap(lua_State *L, Config &config) {
    auto &settings = config.settings;
    bool colored = false;
    if (Section in{L, "snap", "windows.snap"}) {
        boolean(L, "enabled", "windows.snap.enabled", settings.snap);
        settings.snap_distance = integer(L, "distance", settings.snap_distance, 1, 100);
        boolean(L, "corners", "windows.snap.corners", settings.snap_corners);
        boolean(L, "preview", "windows.snap.preview", settings.snap_preview);
        boolean(L, "assist", "windows.snap.assist", settings.snap_assist);
        if (auto color = text(L, "color")) {
            premultiplied(*color, "color", settings.snap_color);
            colored = true;
        }
    }
    if (!colored) {
        for (int i = 0; i < 4; ++i)
            settings.snap_color[i] = settings.border_active[i] * 0.25F;
    }
}
// `windows.shadow = { enabled, color, inactive_color, blur, offset }`; the offset is pixels down
// or { x, y }.
void read_shadow(lua_State *L, Config &config) {
    if (Section in{L, "shadow", "windows.shadow"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "windows.shadow.enabled", settings.shadow);
        for (auto [key, target] : {std::pair{"color", &settings.shadow_color},
                                   {"inactive_color", &settings.shadow_inactive_color}})
            if (auto color = text(L, key))
                premultiplied(*color, key, *target);
        settings.shadow_blur = integer(L, "blur", settings.shadow_blur, 0, 100);
        lua_getfield(L, -1, "offset");
        if (lua_istable(L, -1)) {
            std::tie(settings.shadow_x, settings.shadow_y) =
                integer_pair(L, "windows.shadow.offset", "x", "y", -50, 50);
            lua_pop(L, 1);
        } else {
            lua_pop(L, 1);
            settings.shadow_y = integer(L, "offset", settings.shadow_y, -50, 50);
        }
    }
}
// `windows.blur = { enabled, radius, noise, brightness, saturation }`.
void read_blur(lua_State *L, Config &config) {
    if (Section in{L, "blur", "windows.blur"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "windows.blur.enabled", settings.blur);
        settings.blur_radius = integer(L, "radius", settings.blur_radius, 1, 200);
        settings.blur_noise = static_cast<float>(number(L, "noise", settings.blur_noise, 0, 1));
        settings.blur_brightness =
            static_cast<float>(number(L, "brightness", settings.blur_brightness, 0, 2));
        settings.blur_saturation =
            static_cast<float>(number(L, "saturation", settings.blur_saturation, 0, 2));
    }
}
// A border's colours: `#RRGGBB[AA]`, or a gradient `{ "#RRGGBB[AA]", ..., angle = DEGREES }` of
// 2 to SH_GRADIENT_STOPS colours, whose first colour is also `first`; windows' or a rule's.
void border_colors(lua_State *L, const char *key, float (&first)[4], sh_gradient &gradient) {
    lua_getfield(L, -1, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    std::string label = qualified(key);
    gradient = {};
    if (lua_istable(L, -1)) {
        auto message = label + " must be #RRGGBB, #RRGGBBAA, or a gradient { \"#RRGGBB\", "
                               "\"#RRGGBB\", ..., angle = DEGREES } of 2 to " +
                       std::to_string(SH_GRADIENT_STOPS) + " colours";
        auto count = lua_rawlen(L, -1);
        if (count < 2 || count > SH_GRADIENT_STOPS)
            fail(message, key);
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            bool stop = lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1 &&
                        static_cast<size_t>(lua_tointeger(L, -2)) <= count;
            bool angle = lua_type(L, -2) == LUA_TSTRING && !std::strcmp(lua_tostring(L, -2), "angle");
            if (!stop && !angle)
                fail(message, key);
            lua_pop(L, 1);
        }
        for (size_t i = 1; i <= count; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            premultiplied(string(L, -1, label.c_str()), label.c_str(), gradient.stops[i - 1]);
            lua_pop(L, 1);
        }
        gradient.count = static_cast<int>(count);
        gradient.angle = static_cast<float>(number(L, "angle", 0, -360, 360));
    } else {
        premultiplied(string(L, -1, label.c_str()), label.c_str(), gradient.stops[0]);
        gradient.count = 1;
    }
    std::copy(std::begin(gradient.stops[0]), std::end(gradient.stops[0]), first);
    lua_pop(L, 1);
}
void read_windows(lua_State *L, Config &config) {
    Section section(L, "windows");
    if (!section)
        return;
    config.settings.border_width = integer(L, "border_width", 0, 0, 20);
    config.settings.corner_radius = integer(L, "corner_radius", 10, 0, 40);
    named(L, "round", "windows.round", config.settings.round_always,
          {{"tiling", false}, {"always", true}});
    border_colors(L, "border_color", config.settings.border_active,
                  config.settings.border_active_gradient);
    border_colors(L, "border_inactive_color", config.settings.border_inactive,
                  config.settings.border_inactive_gradient);
    config.opacity = static_cast<float>(number(L, "opacity", 1, 0.05, 1));
    config.inactive_opacity =
        static_cast<float>(number(L, "inactive_opacity", config.opacity, 0.05, 1));
    config.settings.dim_inactive = static_cast<float>(number(L, "dim_inactive", 0, 0, 0.9));
    config.settings.dim_duration = integer(L, "dim_duration", 180, 0, 2000);
    boolean(L, "allow_tearing", "windows.allow_tearing", config.settings.allow_tearing);
    named(L, "activation", "windows.activation", config.settings.activation,
          {{"urgent", SH_ACTIVATION_URGENT},
           {"focus", SH_ACTIVATION_FOCUS},
           {"ignore", SH_ACTIVATION_IGNORE}},
          "windows.activation must be \"focus\", \"urgent\", or \"ignore\"");
    if (auto color = text(L, "urgent_color"))
        premultiplied(*color, "urgent_color", config.settings.urgent_color);
    if (auto buttons = text(L, "buttons", "windows.buttons")) {
        config.window_buttons = *buttons;
        if (config.window_buttons.find_first_not_of("abcdefghijklmnopqrstuvwxyz_,:") !=
            std::string::npos)
            fail("windows.buttons must look like \"appmenu:minimize,maximize,close\"");
    }
    read_swallow(L, config);
    read_magnet(L, config);
    read_snap(L, config);
    read_shadow(L, config);
    read_blur(L, config);
    boolean(L, "lift", "windows.lift", config.settings.lift);
    named(L, "placement", "windows.placement", config.settings.placement,
          {{"cascade", SH_PLACE_CASCADE}, {"center", SH_PLACE_CENTER}, {"smart", SH_PLACE_SMART}});
    config.settings.drag_strip = integer(L, "drag_strip", config.settings.drag_strip, 0, 100);
    named(L, "controls", "windows.controls", config.settings.window_controls,
          {{"flat", SH_CONTROLS_FLAT}, {"traffic_lights", SH_CONTROLS_TRAFFIC_LIGHTS}});
    lua_getfield(L, -1, "rules");
    if (!lua_isnil(L, -1)) {
        auto size = array_size(L, -1, 256);
        for (size_t i = 1; i <= size; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            Named in("windows.rules[" + std::to_string(i) + "]");
            table(L, -1, "window rule");
            keys(L, -1, "windows.rules[]");
            config.window_rules.push_back(window_rule(L, config.settings.workspaces));
            lua_pop(L, 1);
        }
    }
    for (const auto &rule : config.window_rules) {
        if (rule.actions.blur) {
            config.settings.blur_rules = true;
            config.settings.blur_by_rule = config.settings.blur_by_rule || *rule.actions.blur;
        }
    }
    lua_pop(L, 1);
}
// `features = { name = true, ... }`: optional behaviours, each a boolean; unset ones keep
// their defaults from Config. A new one is one row in the list.
void read_features(lua_State *L, Config &config) {
    const std::pair<std::string_view, bool *> features[] = {
        {"workspace_back_and_forth", &config.settings.workspace_back_and_forth},
        {"scratchpad", &config.settings.scratchpad},
        {"sticky", &config.settings.sticky},
        {"keyboard_resize", &config.settings.keyboard_resize},
        {"window_rules", &config.settings.window_rules},
        {"groups", &config.settings.groups},
        {"group_join_new", &config.settings.group_join_new},
    };
    lua_getfield(L, -1, "features");
    if (!lua_isnil(L, -1)) {
        Named in("features");
        table(L, -1, "features");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                fail("features are named, e.g. features = { workspace_back_and_forth = true }");
            std::string name = lua_tostring(L, -2);
            auto found = std::find_if(std::begin(features), std::end(features),
                                      [&](const auto &feature) { return feature.first == name; });
            if (found == std::end(features)) {
                std::vector<std::string> names;
                for (const auto &feature : features)
                    names.emplace_back(feature.first);
                unknown("feature", name, names);
            }
            if (!lua_isboolean(L, -1))
                wrong_type(L, "features." + name, "a boolean");
            *found->second = lua_toboolean(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}
// The action a hot corner's or a swipe's request runs ("workspace 2"), SH_NONE for "none", once
// it has what the action needs; `label` names it in errors, which point at `leaf`.
sh_action request_action(const Config &config, const std::string &request,
                         const std::string &label, const char *leaf) {
    std::istringstream stream(request);
    std::vector<std::string> words{std::istream_iterator<std::string>(stream), {}};
    if (words.empty())
        fail(label + " is blank", leaf);
    if (words[0] == "none")
        return SH_NONE;
    auto action = parse_action(words[0]);
    if (action == SH_SPAWN && words.size() < 2)
        fail(label + " needs a program after spawn", leaf);
    if (action_takes_workspace(action) &&
        !config.workspace_number(join(std::span(words).subspan(1), " ")))
        fail(label + " needs a workspace number or name after " + words[0], leaf);
    return action;
}
// `peek`, `night_light`, `hot_corners`, `zoom`: the desktop effects.
void read_effects(lua_State *L, Config &config) {
    auto &effects = config.settings.effects;
    if (Section in{L, "peek"}) {
        effects.peek_opacity = static_cast<float>(number(L, "opacity", effects.peek_opacity, 0, 0.9));
        effects.peek_duration = integer(L, "duration", effects.peek_duration, 0, 2000);
        boolean(L, "switcher", "peek.switcher", effects.peek_switcher);
    }
    if (Section in{L, "night_light"}) {
        boolean(L, "enabled", "night_light.enabled", effects.night_light);
        effects.day_kelvin = integer(L, "day_temperature", effects.day_kelvin, SH_KELVIN_MIN, SH_KELVIN_MAX);
        effects.night_kelvin = integer(L, "night_temperature", effects.night_kelvin, SH_KELVIN_MIN, SH_KELVIN_MAX);
        effects.transition = number(L, "transition", effects.transition, 0, 240);
        bool seen_sunrise = false, seen_sunset = false;
        for (const char *key : {"sunrise", "sunset"}) {
            if (auto time = text(L, key)) {
                double minutes = 0;
                if (!sh_parse_clock(time->c_str(), &minutes))
                    fail(std::string("night_light.") + key + " must be a time like \"19:30\"", key);
                bool rise = key[3] == 'r';
                (rise ? effects.sunrise : effects.sunset) = minutes;
                (rise ? seen_sunrise : seen_sunset) = true;
            }
        }
        if (present(L, "latitude") || present(L, "longitude")) {
            effects.latitude = number(L, "latitude", effects.latitude, -90, 90);
            effects.longitude = number(L, "longitude", effects.longitude, -180, 180);
            effects.located = true;
        }
        // A location gives the times, unless both were written out.
        if (effects.located && !(seen_sunrise && seen_sunset))
            effects.sunrise = effects.sunset = -1;
    }
    if (Section in{L, "hot_corners"}) {
        effects.corner_size = integer(L, "size", effects.corner_size, 1, 64);
        effects.corner_delay = integer(L, "delay", effects.corner_delay, 0, 5000);
        constexpr const char *corners[] = {"top_left", "top_right", "bottom_left", "bottom_right"};
        for (size_t i = 0; i < 4; ++i) {
            // "" sets nothing, as "none" does.
            auto request = text(L, corners[i]);
            if (!request || request->empty() ||
                request_action(config, *request, std::string("hot_corners.") + corners[i],
                               corners[i]) == SH_NONE)
                continue;
            config.hot_corners[i] = *request;
            effects.corner_mask |= 1U << i;
        }
    }
    if (Section in{L, "zoom"}) {
        effects.zoom_step = static_cast<float>(number(L, "step", effects.zoom_step, 1.05, 4));
        effects.zoom_max = static_cast<float>(number(L, "max", effects.zoom_max, 1.5, 32));
        effects.zoom_duration = integer(L, "duration", effects.zoom_duration, 0, 2000);
        if (auto name = text(L, "scroll_modifier"))
            effects.zoom_scroll_modifier = name->empty() ? 0 : modifier(*name);
    }
}
// `gestures`: the touchpad swipes the compositor takes, each running a request as a hot corner
// does. A list of swipes replaces the default one.
void read_gestures(lua_State *L, Config &config) {
    auto &gestures = config.settings.gestures;
    if (Section in{L, "gestures"}) {
        boolean(L, "enabled", "gestures.enabled", gestures.enabled);
        gestures.distance = integer(L, "distance", gestures.distance, 50, 2000);
        boolean(L, "invert", "gestures.invert", gestures.invert);
        lua_getfield(L, -1, "swipes");
        if (!lua_isnil(L, -1)) {
            Named swipes("gestures.swipes");
            gestures.swipe_count = 0;
            auto size = array_size(L, -1, std::size(gestures.swipes));
            for (size_t i = 1; i <= size; ++i) {
                lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
                table(L, -1, "swipe");
                keys(L, -1, "gestures.swipes[]");
                sh_swipe_binding swipe{};
                swipe.fingers = integer(L, "fingers", 3, 3, 5);
                auto direction = field(L, "direction");
                for (int d = SH_SWIPE_LEFT; d <= SH_SWIPE_DOWN; ++d)
                    if (direction == sh_swipe_direction_name(static_cast<sh_swipe_direction>(d)))
                        swipe.direction = d;
                if (!swipe.direction)
                    unknown("direction", direction, {"left", "right", "up", "down"}, "direction");
                auto request = field(L, "action");
                swipe.action = request_action(config, request, "gestures.swipes action", "action");
                copy_text(request, swipe.request, "gestures.swipes action");
                for (int j = 0; j < gestures.swipe_count; ++j)
                    if (gestures.swipes[j].fingers == swipe.fingers &&
                        gestures.swipes[j].direction == swipe.direction)
                        fail("two swipes of " + std::to_string(swipe.fingers) + " fingers " +
                                 direction,
                             "direction");
                gestures.swipes[gestures.swipe_count++] = swipe;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
}
// A path as the configuration writes it, made absolute: "~/..." in the home directory, else
// relative to the configuration file's `directory`.
std::filesystem::path config_relative(const std::string &path,
                                      const std::filesystem::path &directory) {
    if (path.starts_with("~/")) {
        const char *home = std::getenv("HOME");
        if (!home || !*home)
            fail(qualified("file") + " starts with ~/, but HOME is not set", "file");
        return std::filesystem::path(home) / path.substr(2);
    }
    return std::filesystem::absolute(directory / path).lexically_normal();
}
// Keeps the first error xkbcommon reports, without its message code, in the string that is the
// context's user data, instead of printing it.
__attribute__((format(printf, 3, 0))) void keep_xkb_error(xkb_context *context,
                                                          xkb_log_level level,
                                                          const char *format, va_list args) {
    auto *error = static_cast<std::string *>(xkb_context_get_user_data(context));
    if (!error || !error->empty() || level > XKB_LOG_LEVEL_ERROR)
        return;
    char text[512];
    std::vsnprintf(text, sizeof text, format, args);
    std::string_view message = text;
    if (message.starts_with("[XKB-") && message.find("] ") != std::string_view::npos)
        message.remove_prefix(message.find("] ") + 2);
    while (!message.empty() && (message.back() == '\n' || message.back() == ' '))
        message.remove_suffix(1);
    *error = message;
}
// The compositor builds its keymap from these settings, so a configuration it could not build
// one from is refused here, pointing at the keyboard table, or at keyboard.file and the line of
// the keymap file that xkbcommon stopped at. The names are checked with a file too, as they
// stand in for one that breaks later.
void check_keymap(const sh_settings &settings) {
    Named in("keyboard");
    std::string error;
    std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> context(
        xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref);
    if (!context)
        fail("cannot create XKB context");
    xkb_context_set_user_data(context.get(), &error);
    xkb_context_set_log_fn(context.get(), keep_xkb_error);
    xkb_rule_names names{};
    names.rules = settings.keyboard_rules;
    names.layout = settings.keyboard_layout;
    names.variant = settings.keyboard_variant;
    names.model = settings.keyboard_model;
    names.options = settings.keyboard_options;
    std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> keymap(
        xkb_keymap_new_from_names(context.get(), &names, XKB_KEYMAP_COMPILE_NO_FLAGS),
        xkb_keymap_unref);
    if (!keymap)
        fail("XKB has no keymap for this keyboard layout, variant, model, options and rules" +
             (error.empty() ? "" : ": " + error));
    if (settings.keyboard_file[0]) {
        const std::string path = settings.keyboard_file;
        struct Close {
            void operator()(FILE *file) const { std::fclose(file); }
        };
        std::unique_ptr<FILE, Close> file(std::fopen(path.c_str(), "rb"));
        if (!file)
            fail("keyboard.file: cannot read " + path + ": " + std::strerror(errno), "file");
        std::string text;
        char buffer[4096];
        for (size_t count; (count = std::fread(buffer, 1, sizeof buffer, file.get())) > 0;) {
            text.append(buffer, count);
            if (text.size() > 4 * 1024 * 1024)
                fail("keyboard.file: " + path + " exceeds 4 MiB", "file");
        }
        if (std::ferror(file.get()))
            fail("keyboard.file: cannot read " + path, "file");
        error.clear();
        keymap.reset(xkb_keymap_new_from_string(context.get(), text.c_str(),
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS));
        if (!keymap) {
            // xkbcommon calls what it parsed "(input string)": that is the file.
            auto at = error.find("(input string)");
            if (at != std::string::npos)
                error.replace(at, std::strlen("(input string)"), path);
            else
                error = path + ": " + (error.empty() ? "not an XKB keymap" : error);
            fail("keyboard.file: " + error, "file");
        }
    }
}
void instruction_limit(lua_State *L, lua_Debug *) {
    auto *remaining = static_cast<int *>(lua_getextraspace(L));
    if (--*remaining <= 0) {
        // From now on raised at every instruction: a pcall in the configuration that catches it
        // must not let the configuration run on.
        lua_sethook(L, instruction_limit, LUA_MASKCOUNT, 1);
        luaL_error(L, "configuration exceeded its instruction budget");
    }
}
// The key binding among `bindings` for these modifiers and keysym, or nothing; with `release`,
// the one that runs as the key comes up (a key may have one of each).
const Binding *find_key(const std::vector<Binding> &bindings, uint32_t modifiers, uint32_t keysym,
                        bool release = false) {
    constexpr uint32_t relevant = SH_SHIFT | SH_CTRL | SH_ALT | SH_LOGO;
    modifiers &= relevant; // CapsLock and NumLock do not disable shortcuts.
    keysym = xkb_keysym_to_lower(keysym);
    for (const auto &binding : bindings)
        if (!binding.button && binding.switch_type < 0 && binding.modifiers == modifiers &&
            binding.keysym == keysym && binding.release == release)
            return &binding;
    return nullptr;
}
// The modifier a modifier key gives (SH_LOGO for Super_L), else 0.
uint32_t own_modifier(uint32_t keysym) {
    switch (keysym) {
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
        return SH_SHIFT;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
        return SH_CTRL;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
        return SH_ALT;
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
        return SH_LOGO;
    default:
        return 0;
    }
}
// The binding among `bindings` of a switch turning on or off, or nothing.
const Binding *find_switch(const std::vector<Binding> &bindings, sh_switch type, bool on) {
    for (const auto &binding : bindings)
        if (binding.switch_type == static_cast<int>(type) && binding.switch_on == on)
            return &binding;
    return nullptr;
}
// Reads the list of bindings on top of the stack, when there is one, into `into`; `in_mode` for a
// mode's, which take keys alone.
void read_bindings(lua_State *L, Config &config, std::vector<Binding> &into, size_t own,
                   bool in_mode) {
    if (!lua_isnil(L, -1)) {
        auto size = array_size(L, -1, 512);
        for (size_t i = 1; i <= size; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            Named in(current_section + "[" + std::to_string(i) + "]");
            table(L, -1, "binding");
            keys(L, -1, "bindings[]");
            Binding binding{};
            bool is_button = present(L, "button");
            if (is_button && in_mode)
                fail("a mode's bindings take a key, not a button");
            bool is_switch = present(L, "switch");
            if (is_switch && in_mode)
                fail("a mode's bindings take a key, not a switch");
            if (is_switch) {
                read_switch_binding(L, binding);
                // A switch's binding runs while the session is locked, and never repeats.
                for (const char *only : {"locked", "repeats", "release"})
                    if (present(L, only))
                        fail(std::string(only) + " is only valid with a key");
            } else if (is_button) {
                if (present(L, "key"))
                    fail("a binding takes a key or a button, not both");
                binding.button = mouse_button(field(L, "button"));
                binding.pattern = pattern_field(L, "app_id", binding.app_id);
                boolean(L, "desktop", "desktop", binding.desktop);
                for (const char *only : {"locked", "repeats", "release"})
                    if (present(L, only))
                        fail(std::string(only) + " is only valid with a key");
            } else {
                for (const char *only : {"app_id", "desktop"})
                    if (present(L, only))
                        fail(std::string(only) + " is only valid with a button");
                auto key = field(L, "key");
                binding.keysym =
                    xkb_keysym_to_lower(xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS));
                if (binding.keysym == XKB_KEY_NoSymbol)
                    fail("unknown key '" + key + "'");
            }
            if (!is_switch && present(L, "state"))
                fail("state is only valid with a switch");
            if (auto description = text(L, "description"))
                binding.description = *description;
            auto action = field(L, "action");
            binding.action = action == "none" ? SH_NONE : parse_action(action);
            lua_getfield(L, -1, "mods");
            if (!lua_isnil(L, -1))
                for (const auto &name : strings(L, 4, "modifier")) {
                    auto bit = modifier(name);
                    if (binding.modifiers & bit)
                        fail("duplicate modifier");
                    binding.modifiers |= bit;
                }
            lua_pop(L, 1);
            lua_getfield(L, -1, "command");
            if (binding.action == SH_SPAWN)
                binding.command = command(L);
            else if (!lua_isnil(L, -1))
                fail("command is only valid with spawn");
            lua_pop(L, 1);
            if (action_takes_workspace(binding.action)) {
                lua_getfield(L, -1, "workspace");
                bool named = lua_type(L, -1) == LUA_TSTRING;
                if (named) {
                    auto name = string(L, -1, "workspace");
                    binding.workspace = config.workspace_number(name);
                    if (!binding.workspace)
                        fail("no workspace named '" + name + "' in layout.workspace_names");
                }
                lua_pop(L, 1);
                if (!named)
                    binding.workspace = integer(L, "workspace", 0, 1, config.settings.workspaces);
                if (binding.workspace == 0)
                    fail("workspace actions need a workspace number");
            } else if (present(L, "workspace")) {
                fail("workspace is only valid with workspace actions");
            }
            lua_getfield(L, -1, "output");
            if (action_takes_output(binding.action)) {
                binding.output = lua_isnil(L, -1) ? (binding.action == SH_SWAP_WORKSPACES ? "next" : "")
                                                  : string(L, -1, "output");
                if (!valid_output_target(binding.output))
                    fail(std::string(binding.action == SH_SWAP_WORKSPACES ? "swap_workspaces"
                                                                           : "move_workspace_to_output") +
                         " needs an output: \"left\", \"right\", \"next\", \"prev\", or a "
                         "connector name");
            } else if (action_takes_display(binding.action)) {
                // Without one, every monitor.
                if (!lua_isnil(L, -1)) {
                    binding.output = string(L, -1, "output");
                    if (!valid_output_target(binding.output))
                        fail("output must be a monitor's connector name or \"desc:\" and the "
                             "start of its description");
                }
            } else if (!lua_isnil(L, -1)) {
                fail("output is only valid with move_workspace_to_output, swap_workspaces and "
                     "the display_* actions");
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "mode");
            if (binding.action == SH_MODE) {
                // Every mode's name is known by now, read before any binding.
                auto name = lua_isnil(L, -1) ? std::string() : string(L, -1, "mode");
                binding.mode = config.mode_number(name);
                if (binding.mode < 0 && name.empty())
                    fail("the mode action needs mode = \"default\" or the name of one of modes");
                if (binding.mode < 0)
                    unknown("mode", name, config.mode_names(), "=" + name);
            } else if (binding.action == SH_DISPLAY_MODE) {
                // Without one, the popup that steps through them.
                if (!lua_isnil(L, -1))
                    binding.mode = parse_display_mode(string(L, -1, "mode"));
            } else if (binding.action == SH_CAT) {
                binding.mode = lua_isnil(L, -1) ? SH_CAT_TOGGLE
                                                : parse_cat_switch(string(L, -1, "mode"));
            } else if (binding.action == SH_HEATMAP) {
                // Without one, it toggles.
                if (!lua_isnil(L, -1))
                    binding.mode = parse_heatmap_choice(string(L, -1, "mode"));
            } else if (binding.action == SH_AMBIENT) {
                binding.mode = lua_isnil(L, -1) ? SH_AMBIENT_TOGGLE
                                                : parse_ambient_choice(string(L, -1, "mode"));
            } else if (binding.action == SH_CURSOR_TRAIL) {
                binding.mode = lua_isnil(L, -1)
                                   ? SH_CURSOR_TRAIL_TOGGLE
                                   : parse_cursor_trail_choice(string(L, -1, "mode"));
            } else if (!lua_isnil(L, -1)) {
                if (binding.action != SH_SCREENSHOT)
                    fail("mode is only valid with screenshot, mode, display_mode, cat, heatmap, "
                         "ambient and cursor_trail");
                binding.screenshot = parse_screenshot_mode(string(L, -1, "mode"));
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "layout");
            bool has_layout = !lua_isnil(L, -1), numbered = lua_type(L, -1) == LUA_TNUMBER;
            if (has_layout && !numbered && lua_type(L, -1) != LUA_TSTRING)
                wrong_type(L, "layout", "\"next\", \"prev\" or a number");
            auto choice = has_layout && !numbered ? string(L, -1, "layout") : "";
            lua_pop(L, 1);
            if (has_layout && binding.action != SH_SWITCH_LAYOUT)
                fail("layout is only valid with switch_layout");
            if (numbered)
                binding.layout = integer(L, "layout", 0, 1, max_layouts);
            else if (has_layout)
                binding.layout = parse_layout_choice(choice);
            if (present(L, "amount") && !action_takes_amount(binding.action))
                fail("amount is only valid with resize, volume and brightness actions");
            binding.amount = integer(L, "amount", default_amount(binding.action), 1,
                                     max_amount(binding.action));
            boolean(L, "release", "release", binding.release);
            // Keyboard resizing repeats unless told not to; other actions only when told.
            binding.repeats = !binding.button && !is_switch && !binding.release &&
                              binding.action >= SH_RESIZE_LEFT && binding.action <= SH_RESIZE_DOWN;
            boolean(L, "locked", "locked", binding.locked);
            boolean(L, "repeats", "repeats", binding.repeats);
            if (binding.release && binding.repeats)
                fail("a binding that runs on release does not repeat");
            // A modifier key's own modifier is not yet held as it goes down: Super + Super_L is
            // Super_L.
            if (binding.release)
                binding.modifiers &= ~own_modifier(binding.keysym);
            // Bindings past `own` come from the defaults a configuration extends; its own
            // bindings, "none" included, take their keys first.
            // Button bindings may share a button: the first whose target matches wins.
            bool taken = is_switch ? find_switch(into, static_cast<sh_switch>(binding.switch_type),
                                                 binding.switch_on) != nullptr
                                   : !binding.button &&
                                         find_key(into, binding.modifiers, binding.keysym,
                                                  binding.release);
            if (!taken)
                into.push_back(std::move(binding));
            else if (i <= own)
                fail(is_switch ? "duplicate switch binding" : "duplicate keyboard binding");
            lua_pop(L, 1);
        }
    }
}
constexpr size_t max_modes = 16;
// A mode's or a profile's name: 1 to 32 letters, digits, '-' and '_', and none of `reserved`.
// Names travel through `paw msg mode NAME`, `paw msg profile NAME` and the shell's state, so
// they hold no spaces.
bool valid_name(const std::string &name, std::initializer_list<std::string_view> reserved) {
    if (name.empty() || name.size() > 32 ||
        std::find(reserved.begin(), reserved.end(), name) != reserved.end())
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
    });
}
// The names in `modes`, sorted, after checking its shape. Their bindings are read once every
// binding outside them is, since any may name a mode.
void read_mode_names(lua_State *L, Config &config) {
    lua_getfield(L, -1, "modes");
    if (!lua_isnil(L, -1)) {
        Named in("modes");
        table(L, -1, "modes");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                fail("modes must be keyed by name");
            std::string name = lua_tostring(L, -2);
            // "default" is the bindings outside any mode.
            if (!valid_name(name, {"default"}))
                fail("mode name '" + name +
                         "' must be 1 to 32 letters, digits, '-' or '_', and not default",
                     name);
            config.modes.push_back({name, {}});
            lua_pop(L, 1);
        }
        if (config.modes.size() > max_modes)
            fail("at most 16 modes");
        std::sort(config.modes.begin(), config.modes.end(),
                  [](const Mode &a, const Mode &b) { return a.name < b.name; });
    }
    lua_pop(L, 1);
}
// Each mode's bindings. A mode that nothing leaves would keep every other key binding away.
void read_modes(lua_State *L, Config &config) {
    Named in("modes");
    for (auto &mode : config.modes) {
        Named in_mode("modes." + mode.name);
        lua_getfield(L, -1, "modes");
        lua_getfield(L, -1, mode.name.c_str());
        table(L, -1, ("modes." + mode.name).c_str());
        read_bindings(L, config, mode.bindings, SIZE_MAX, true);
        lua_pop(L, 2);
        std::erase_if(mode.bindings,
                      [](const Binding &binding) { return binding.action == SH_NONE; });
        if (std::none_of(mode.bindings.begin(), mode.bindings.end(),
                         [](const Binding &binding) { return binding.action == SH_MODE; }))
            fail("mode '" + mode.name + "' has no binding with action = \"mode\" to leave it",
                 mode.name);
    }
}
// appearance.cursor_trail, on top of the stack.
void read_cursor_trail(lua_State *L, sh_settings &settings) {
    named(L, "kind", "appearance.cursor_trail.kind", settings.cursor_trail,
          {{"none", SH_TRAIL_NONE},
           {"sparkles", SH_TRAIL_SPARKLES},
           {"petals", SH_TRAIL_PETALS},
           {"hearts", SH_TRAIL_HEARTS}},
          "appearance.cursor_trail.kind must be \"none\", \"sparkles\", \"petals\" or "
          "\"hearts\"",
          "kind");
    settings.cursor_trail_amount =
        static_cast<float>(number(L, "amount", settings.cursor_trail_amount, 0.25, 4));
    lua_getfield(L, -1, "colors");
    if (!lua_isnil(L, -1)) {
        Named in("appearance.cursor_trail.colors");
        auto message = "appearance.cursor_trail.colors must be a list of 1 to " +
                       std::to_string(SH_TRAIL_TINTS) + " colours, \"#RRGGBB\"";
        if (!lua_istable(L, -1) || lua_rawlen(L, -1) < 1 || lua_rawlen(L, -1) > SH_TRAIL_TINTS)
            fail(message, "colors");
        auto colors = strings(L, SH_TRAIL_TINTS, "appearance.cursor_trail.colors");
        settings.cursor_trail_tints = 0;
        for (const auto &color : colors) {
            if (!is_color(color))
                fail(message, "colors");
            auto &target = settings.cursor_trail_colors[settings.cursor_trail_tints++];
            for (size_t i = 0; i < 3; ++i)
                target[i] = std::stoi(color.substr(1 + 2 * i, 2), nullptr, 16) / 255.0F;
        }
    }
    lua_pop(L, 1);
}
// The colours the cursor trail comes in when appearance.cursor_trail.colors gives none: the
// focused border's (the first three of a gradient's) and shell.accent, leaving out those close to
// one already taken, and a light blue or a sakura pink beside them when none of them is bluish or
// pinkish.
void default_trail_colors(Config &config) {
    auto &settings = config.settings;
    if (settings.cursor_trail_tints > 0)
        return;
    std::vector<std::array<float, 3>> colors;
    auto add = [&colors](std::array<float, 3> color) {
        for (const auto &kept : colors) {
            float distance = 0;
            for (size_t i = 0; i < 3; ++i)
                distance += (kept[i] - color[i]) * (kept[i] - color[i]);
            if (distance < 0.01F)
                return;
        }
        colors.push_back(color);
    };
    const auto &border = settings.border_active_gradient;
    for (int i = 0; i < std::min(border.count, 3); ++i) {
        float alpha = border.stops[i][3];
        if (alpha > 0.05F)
            add({std::min(border.stops[i][0] / alpha, 1.0F), std::min(border.stops[i][1] / alpha, 1.0F),
                 std::min(border.stops[i][2] / alpha, 1.0F)});
    }
    if (is_color(config.shell.accent, true)) {
        std::array<float, 3> accent{};
        for (size_t i = 0; i < 3; ++i)
            accent[i] = std::stoi(config.shell.accent.substr(1 + 2 * i, 2), nullptr, 16) / 255.0F;
        add(accent);
    }
    auto leaning = [&colors](int more, int less) {
        return std::any_of(colors.begin(), colors.end(),
                           [&](const auto &color) { return color[more] > color[less] + 0.05F; });
    };
    if (!leaning(2, 0))
        add({0x8f / 255.0F, 0xc1 / 255.0F, 0xec / 255.0F}); // light blue
    if (!leaning(0, 2))
        add({0xf2 / 255.0F, 0xa0 / 255.0F, 0xbe / 255.0F}); // sakura pink
    settings.cursor_trail_tints = 0;
    for (const auto &color : colors) {
        if (settings.cursor_trail_tints == SH_TRAIL_TINTS)
            break;
        std::copy(color.begin(), color.end(),
                  settings.cursor_trail_colors[settings.cursor_trail_tints++]);
    }
}
// appearance.KEY, a cursor or icon theme: the name of a folder of an icons directory, so not a
// path; "" for the session's own, as unset.
template <std::size_t N> void theme_name(lua_State *L, const char *key, char (&target)[N]) {
    const auto label = std::string("appearance.") + key;
    auto name = text(L, key, label.c_str());
    if (!name)
        return;
    if (name->find('/') != std::string::npos || *name == "." || *name == "..")
        fail(label + " must be a theme's name, such as \"Adwaita\", not a path", key);
    copy_text(*name, target, label);
}
// `directory` is the configuration file's, which relative paths in it start from.
Config read(lua_State *L, size_t own, const std::filesystem::path &directory) {
    Config config;
    table(L, -1, "configuration result");
    keys(L, -1, "");
    read_shell(L, config.shell);
    // The shell's colours, which the compositor draws the colour picker's loupe in.
    straight(config.shell.panel_color, config.settings.shell_surface);
    config.settings.shell_surface[3] = 1;
    straight(config.shell.text_color, config.settings.shell_text);
    straight(config.shell.accent, config.settings.shell_accent);
    read_features(L, config);
    if (integer(L, "version", 1, 1, 1) != 1)
        fail("unsupported version");
    boolean(L, "xwayland", "xwayland", config.settings.xwayland);
    boolean(L, "auto_reload", "auto_reload", config.auto_reload);
    lua_getfield(L, -1, "terminal");
    if (!lua_isnil(L, -1)) {
        config.terminal = strings(L, 64, "terminal argument");
        if (config.terminal.empty() || config.terminal.front().empty())
            fail("terminal needs a program, such as { \"foot\" }", "terminal");
    }
    lua_pop(L, 1);
    if (Section in{L, "appearance"}) {
        if (auto color = text(L, "background")) {
            if (!is_color(*color))
                fail("background must be #RRGGBB");
            for (size_t i = 0; i < 3; ++i)
                config.settings.background[i] =
                    std::stoi(color->substr(1 + 2 * i, 2), nullptr, 16) / 255.0F;
        }
        named(L, "color_scheme", "appearance.color_scheme", config.settings.color_scheme,
              {{"light", SH_COLOR_SCHEME_LIGHT}, {"dark", SH_COLOR_SCHEME_DARK}},
              "appearance.color_scheme must be \"light\" or \"dark\"", "color_scheme");
        theme_name(L, "cursor_theme", config.settings.cursor_theme);
        config.settings.cursor_size = integer(L, "cursor_size", 0, 0, 256);
        theme_name(L, "icon_theme", config.settings.icon_theme);
        if (Section ambient{L, "ambient", "appearance.ambient"}) {
            auto &settings = config.settings;
            named(L, "kind", "appearance.ambient.kind", settings.ambient,
                  {{"none", SH_AMBIENT_NONE},
                   {"sakura", SH_AMBIENT_SAKURA},
                   {"snow", SH_AMBIENT_SNOW},
                   {"fireflies", SH_AMBIENT_FIREFLIES},
                   {"embers", SH_AMBIENT_EMBERS},
                   {"rain", SH_AMBIENT_RAIN}},
                  "appearance.ambient.kind must be \"none\", \"sakura\", \"snow\", "
                  "\"fireflies\", \"embers\" or \"rain\"",
                  "kind");
            settings.ambient_count = integer(L, "count", settings.ambient_count, 1, 150);
            settings.ambient_speed =
                static_cast<float>(number(L, "speed", settings.ambient_speed, 0.1, 5));
        }
        if (Section trail{L, "cursor_trail", "appearance.cursor_trail"})
            read_cursor_trail(L, config.settings);
    }
    if (Section in{L, "keyboard"}) {
        text_field(L, "layout", config.settings.keyboard_layout);
        text_field(L, "variant", config.settings.keyboard_variant);
        text_field(L, "model", config.settings.keyboard_model);
        text_field(L, "options", config.settings.keyboard_options);
        text_field(L, "rules", config.settings.keyboard_rules);
        if (auto file = text(L, "file", "keyboard.file"); file && !file->empty())
            copy_text(config_relative(*file, directory).string(), config.settings.keyboard_file,
                      "keyboard.file");
        config.settings.repeat_rate = integer(L, "repeat_rate", 25, 0, 100);
        config.settings.repeat_delay = integer(L, "repeat_delay", 600, 0, 5000);
        boolean(L, "shortcuts_inhibit", "keyboard.shortcuts_inhibit",
                config.settings.shortcuts_inhibit);
    }
    if (Section in{L, "mouse"}) {
        if (auto name = text(L, "modifier"))
            config.settings.mouse_modifier = modifier(*name);
        lua_getfield(L, -1, "middle_drag");
        if (!lua_isnil(L, -1)) {
            config.settings.middle_drag_count = 0;
            for (const auto &name : strings(L, 32, "mouse.middle_drag"))
                copy_text(name, config.settings.middle_drag[config.settings.middle_drag_count++],
                          "mouse.middle_drag entry");
        }
        lua_pop(L, 1);
        config.settings.pointer_speed_set = present(L, "speed");
        config.settings.pointer_speed = number(L, "speed", 0, -1, 1);
        named(L, "acceleration", nullptr, config.settings.pointer_accel,
              {{"flat", 0}, {"adaptive", 1}}, "mouse.acceleration must be \"flat\" or \"adaptive\"");
        tristate(L, "natural_scroll", "mouse.natural_scroll", config.settings.mouse_natural_scroll);
        boolean(L, "focus_follows", "mouse.focus_follows", config.settings.focus_follows_mouse);
        boolean(L, "hide_when_typing", "mouse.hide_when_typing",
                config.settings.hide_cursor_typing);
        config.settings.hide_cursor_after = integer(L, "hide_after", 0, 0, 3600);
        boolean(L, "shake_to_find", "mouse.shake_to_find", config.settings.shake_to_find);
    }
    if (Section in{L, "touchpad"}) {
        tristate(L, "natural_scroll", "touchpad.natural_scroll",
                 config.settings.touchpad_natural_scroll);
        tristate(L, "tap_to_click", "touchpad.tap_to_click", config.settings.touchpad_tap);
        tristate(L, "disable_while_typing", "touchpad.disable_while_typing",
                 config.settings.touchpad_dwt);
    }
    if (Section in{L, "touch"}) {
        text_field(L, "output", config.settings.touch_output);
        if (!std::strcmp(config.settings.touch_output, "desc:"))
            fail("touch.output needs a description after desc:", "output");
    }
    if (Section in{L, "tablet"}) {
        text_field(L, "output", config.settings.tablet_output);
        if (!std::strcmp(config.settings.tablet_output, "desc:"))
            fail("tablet.output needs a description after desc:", "output");
    }
    if (Section in{L, "layout"}) {
        // gap sets both; gap_inner and gap_outer override it.
        int gap = integer(L, "gap", 8, 0, 100);
        config.settings.gap_inner = integer(L, "gap_inner", gap, 0, 100);
        config.settings.gap_outer = integer(L, "gap_outer", gap, 0, 100);
        boolean(L, "smart_gaps", "layout.smart_gaps", config.settings.smart_gaps);
        // One number for every side, or { top, right, bottom, left } by name, the sides left
        // out 0.
        lua_getfield(L, -1, "lone_margin");
        if (lua_isinteger(L, -1)) {
            auto margin = lua_tointeger(L, -1);
            if (margin < 0 || margin > 400)
                out_of_range("lone_margin", static_cast<double>(margin), 0, 400);
            for (auto &side : config.settings.lone_margin)
                side = static_cast<int>(margin);
        } else if (!lua_isnil(L, -1)) {
            table(L, -1, "layout.lone_margin");
            keys(L, -1, "layout.lone_margin");
            Named margin("layout.lone_margin");
            int index = 0;
            for (const char *side : {"top", "right", "bottom", "left"})
                config.settings.lone_margin[index++] = integer(L, side, 0, 0, 400);
        }
        lua_pop(L, 1);
        boolean(L, "floating_above_tiles", "layout.floating_above_tiles",
                config.settings.floating_above_tiles);
        boolean(L, "tiling", "layout.tiling", config.settings.tiling);
        boolean(L, "tiling_per_workspace", "layout.tiling_per_workspace",
                config.settings.tiling_per_workspace);
        boolean(L, "maximize_lone_on_untile", "layout.maximize_lone_on_untile",
                config.settings.maximize_lone_on_untile);
        config.settings.workspaces = integer(L, "workspaces", 4, 1, 10);
        lua_getfield(L, -1, "workspace_names");
        if (!lua_isnil(L, -1)) {
            if (array_size(L, -1, 10) > static_cast<size_t>(config.settings.workspaces))
                fail("layout.workspace_names has more names than layout.workspaces");
            for (auto &name : strings(L, 10, "workspace name")) {
                if (name.size() > 32)
                    fail("workspace name '" + name + "' is longer than 32 characters");
                bool digits = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
                    return std::isdigit(static_cast<unsigned char>(c));
                });
                if (digits)
                    fail("workspace name '" + name + "' would be mistaken for a number");
                for (unsigned char c : name)
                    if (c < 0x20 || c == 0x7f)
                        fail("workspace names cannot contain control characters");
                if (!name.empty() && (name.front() == ' ' || name.back() == ' '))
                    fail("workspace name '" + name + "' starts or ends with a space");
                if (!name.empty() && std::find(config.workspace_names.begin(),
                                               config.workspace_names.end(),
                                               name) != config.workspace_names.end())
                    fail("duplicate workspace name '" + name + "'");
                config.workspace_names.push_back(std::move(name));
            }
        }
        lua_pop(L, 1);
        if (auto layout = text(L, "tile_layout", "layout.tile_layout"))
            config.settings.tile_layout = tile_layout_index(*layout);
        config.settings.master_ratio = static_cast<float>(number(L, "master_ratio", 0.55, 0.1, 0.9));
        config.settings.master_count = integer(L, "master_count", 1, 1, 8);
        if (Section scroll{L, "scroll", "layout.scroll"})
            read_scroll(L, config.settings);
        read_output_layouts(L, config.settings);
    }
    if (Section in{L, "outputs"}) {
        boolean(L, "return_windows", "outputs.return_windows", config.settings.return_windows);
        named(L, "lid", "outputs.lid", config.settings.lid,
              {{"clamshell", SH_LID_CLAMSHELL}, {"ignore", SH_LID_IGNORE}},
              "outputs.lid must be \"clamshell\" or \"ignore\"", "lid");
        read_monitors(L, config.settings);
        lua_getfield(L, -1, "order");
        if (!lua_isnil(L, -1)) {
            auto names = strings(L, std::size(config.settings.output_order), "output name");
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i].empty())
                    fail("output name is empty");
                for (size_t j = 0; j < i; ++j)
                    if (names[i] == config.settings.output_order[j])
                        fail("duplicate output '" + names[i] + "'");
                copy_text(names[i], config.settings.output_order[i], "output name");
            }
            config.settings.output_count = static_cast<int>(names.size());
        }
        lua_pop(L, 1);
        if (auto name = text(L, "primary")) {
            if (name->empty())
                fail("primary output name is empty");
            copy_text(*name, config.settings.primary_output, "primary output name");
        }
    }
    read_windows(L, config);
    if (Section in{L, "animations"}) {
        boolean(L, "enabled", "animations.enabled", config.settings.animations);
        read_animations(L, config.settings);
    }
    read_effects(L, config);
    read_gestures(L, config);
    if (Section in{L, "overview"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "overview.enabled", settings.overview);
        settings.overview_gap = integer(L, "gap", settings.overview_gap, 0, 200);
        boolean(L, "animation", "overview.animation", settings.overview_animation);
        settings.overview_duration = integer(L, "duration", settings.overview_duration, 10, 1000);
        boolean(L, "strip", "overview.strip", settings.overview_strip);
        named(L, "hot_corner", "overview.hot_corner", settings.overview_hot_corner,
              {{"none", 0}, {"top-left", 1}, {"top-right", 2}, {"bottom-left", 3},
               {"bottom-right", 4}},
              "overview.hot_corner must be \"none\", \"top-left\", \"top-right\", "
              "\"bottom-left\", or \"bottom-right\"");
        // A corner runs one thing: this or what hot_corners gives it, not both.
        if (int corner = settings.overview_hot_corner - 1;
            corner >= 0 && settings.effects.corner_mask & (1U << corner)) {
            constexpr const char *keys[] = {"top_left", "top_right", "bottom_left",
                                            "bottom_right"};
            constexpr const char *names[] = {"top-left", "top-right", "bottom-left",
                                             "bottom-right"};
            fail(std::string("overview.hot_corner and hot_corners.") + keys[corner] +
                     " both claim the " + names[corner] + " corner",
                 "hot_corner");
        }
        settings.overview_dim = static_cast<float>(number(L, "dim", settings.overview_dim, 0, 1));
    }
    if (Section in{L, "switcher"})
        boolean(L, "hold", "switcher.hold", config.settings.switcher_hold);
    if (Section in{L, "notifications"}) {
        auto &notifications = config.notifications;
        boolean(L, "enabled", "notifications.enabled", notifications.enabled);
        named(L, "position", "notifications.position", notifications.position,
              {{"top-right", Corner::TopRight},
               {"top-left", Corner::TopLeft},
               {"bottom-right", Corner::BottomRight},
               {"bottom-left", Corner::BottomLeft}},
              "notifications.position must be \"top-right\", \"top-left\", \"bottom-right\", or "
              "\"bottom-left\"",
              "position");
        notifications.timeout = integer(L, "timeout", notifications.timeout, 0, 600000);
        notifications.max_visible = integer(L, "max_visible", notifications.max_visible, 1, 10);
        boolean(L, "dnd", "notifications.dnd", notifications.dnd);
        notifications.width = integer(L, "width", notifications.width, 200, 800);
        notifications.history = integer(L, "history", notifications.history, 0, 1000);
    }
    if (Section in{L, "osd"}) {
        auto &osd = config.osd;
        boolean(L, "enabled", "osd.enabled", osd.enabled);
        named(L, "position", "osd.position", osd.top, {{"top", true}, {"bottom", false}},
              "osd.position must be \"top\" or \"bottom\"", "position");
        osd.timeout = integer(L, "timeout", osd.timeout, 200, 10000);
        boolean(L, "volume", "osd.volume", osd.volume);
        boolean(L, "brightness", "osd.brightness", osd.brightness);
    }
    if (Section in{L, "screenshots"}) {
        if (auto folder = text(L, "directory", "screenshots.directory")) {
            if (!folder->starts_with('/') && !folder->starts_with("~/"))
                fail("screenshots.directory must be absolute or start with ~/");
            config.screenshots.directory = *folder;
        }
        boolean(L, "clipboard", "screenshots.clipboard", config.screenshots.clipboard);
        boolean(L, "notify", "screenshots.notify", config.screenshots.notify);
    }
    if (Section in{L, "power"}) {
        lua_getfield(L, -1, "lock_command");
        if (!lua_isnil(L, -1)) {
            // A program and its arguments, or {} for no locker.
            auto locker = strings(L, 64, "power.lock_command argument");
            if (!locker.empty() && locker.front().empty())
                fail("power.lock_command's program is empty", "lock_command");
            config.power.lock_command = std::move(locker);
        }
        lua_pop(L, 1);
        boolean(L, "lock_style", "power.lock_style", config.power.lock_style);
        boolean(L, "lock_before_sleep", "power.lock_before_sleep",
                config.settings.lock_before_sleep);
        boolean(L, "close_windows", "power.close_windows", config.settings.close_windows);
        config.settings.close_timeout =
            integer(L, "close_timeout", config.settings.close_timeout, 500, 60000);
        boolean(L, "force", "power.force", config.settings.close_force);
        config.power.countdown = integer(L, "countdown", config.power.countdown, 0, 300);
    }
    read_mode_names(L, config);
    read_idle(L, config.settings);
    {
        Named in("bindings");
        lua_getfield(L, -1, "bindings");
        read_bindings(L, config, config.bindings, own, false);
        lua_pop(L, 1);
    }
    std::erase_if(config.bindings,
                  [](const Binding &binding) {
                      // A button's "none" stays: it hands matching clicks to the application.
                      return binding.action == SH_NONE && !binding.button;
                  });
    read_modes(L, config);
    lua_getfield(L, -1, "startup");
    if (!lua_isnil(L, -1)) {
        auto size = array_size(L, -1, 32);
        for (size_t i = 1; i <= size; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            config.startup.push_back(command(L));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    if (Section in{L, "autostart"}) {
        boolean(L, "xdg", "autostart.xdg", config.autostart.xdg);
        lua_getfield(L, -1, "exclude");
        if (!lua_isnil(L, -1))
            for (auto &name : strings(L, 128, "autostart.exclude entry")) {
                if (name.find('/') != std::string::npos || !name.ends_with(".desktop") ||
                    name.size() == 8)
                    fail("autostart.exclude names a desktop file by its file name, such as "
                         "\"foo.desktop\", not '" + name + "'",
                         "exclude");
                config.autostart.exclude.push_back(std::move(name));
            }
        lua_pop(L, 1);
    }
    if (Section in{L, "session"}) {
        named(L, "restore", "session.restore", config.settings.session_restore,
              {{"off", SH_SESSION_RESTORE_OFF},
               {"windows", SH_SESSION_RESTORE_WINDOWS},
               {"launch", SH_SESSION_RESTORE_LAUNCH}},
              "session.restore must be \"off\", \"windows\" or \"launch\"", "restore");
    }
    if (Section in{L, "cat"}) {
        auto &settings = config.settings;
        boolean(L, "enabled", "cat.enabled", settings.cat);
        settings.cat_scale = integer(L, "scale", settings.cat_scale, 1, 4);
        named(L, "coat", "cat.coat", settings.cat_coat,
              {{"auto", -1},
               {"ginger", SH_CAT_COAT_GINGER},
               {"black", SH_CAT_COAT_INK},
               {"cream", SH_CAT_COAT_CREAM},
               {"grey", SH_CAT_COAT_GREY}},
              "cat.coat must be \"auto\", \"ginger\", \"black\", \"cream\" or \"grey\"", "coat");
        settings.cat_sleep = integer(L, "sleep_after", settings.cat_sleep, 5, 3600);
    }
    // `profile_schedule`; that its names are profiles is checked once every profile is known.
    if (Section in{L, "profile_schedule"}) {
        auto &schedule = config.settings.profile_schedule;
        auto day = text(L, "day"), night = text(L, "night");
        if (!day || !night)
            fail("profile_schedule needs a day and a night profile, as "
                 "{ day = \"light\", night = \"dark\" }");
        copy_text(*day, schedule.day, "profile_schedule.day");
        copy_text(*night, schedule.night, "profile_schedule.night");
        auto sunrise = text(L, "sunrise"), sunset = text(L, "sunset");
        if (sunrise.has_value() != sunset.has_value())
            fail("profile_schedule.sunrise and sunset go together: set both, or neither to "
                 "follow night_light's",
                 sunrise ? "sunrise" : "sunset");
        if (sunrise) {
            if (!sh_parse_clock(sunrise->c_str(), &schedule.sunrise))
                fail("profile_schedule.sunrise must be a time like \"07:00\"", "sunrise");
            if (!sh_parse_clock(sunset->c_str(), &schedule.sunset))
                fail("profile_schedule.sunset must be a time like \"20:00\"", "sunset");
            if (schedule.sunrise == schedule.sunset)
                fail("profile_schedule.sunrise and sunset must differ", "sunset");
            schedule.own_times = true;
        }
    }
    if (Section in{L, "wrapped"})
        boolean(L, "enabled", "wrapped.enabled", config.settings.wrapped);
    if (Section in{L, "heatmap"})
        boolean(L, "enabled", "heatmap.enabled", config.settings.heatmap);
    default_trail_colors(config);
    return config;
}
} // namespace

namespace {
constexpr std::pair<std::string_view, sh_action> action_table[] = {
        {"spawn", SH_SPAWN},
        {"terminal", SH_TERMINAL},
        {"quit", SH_QUIT},
        {"close", SH_CLOSE},
        {"cycle", SH_CYCLE},
        {"snap_left", SH_SNAP_LEFT},
        {"snap_right", SH_SNAP_RIGHT},
        {"snap_top_left", SH_SNAP_TOP_LEFT},
        {"snap_top_right", SH_SNAP_TOP_RIGHT},
        {"snap_bottom_left", SH_SNAP_BOTTOM_LEFT},
        {"snap_bottom_right", SH_SNAP_BOTTOM_RIGHT},
        {"snap_cycle_left", SH_SNAP_CYCLE_LEFT},
        {"snap_cycle_right", SH_SNAP_CYCLE_RIGHT},
        {"snap_cycle_up", SH_SNAP_CYCLE_UP},
        {"snap_cycle_down", SH_SNAP_CYCLE_DOWN},
        {"maximize", SH_MAXIMIZE},
        {"restore", SH_RESTORE},
        {"minimize", SH_MINIMIZE},
        {"tile", SH_TILE},
        {"reload", SH_RELOAD},
        {"fullscreen", SH_FULLSCREEN},
        {"workspace", SH_WORKSPACE},
        {"move_to_workspace", SH_MOVE_TO_WORKSPACE},
        {"move_to_workspace_next", SH_MOVE_TO_WORKSPACE_NEXT},
        {"move_to_workspace_prev", SH_MOVE_TO_WORKSPACE_PREV},
        {"move_to_workspace_empty", SH_MOVE_TO_WORKSPACE_EMPTY},
        {"workspace_next", SH_WORKSPACE_NEXT},
        {"workspace_prev", SH_WORKSPACE_PREV},
        {"workspace_back", SH_WORKSPACE_BACK},
        {"toggle_tiling", SH_TOGGLE_TILING},
        {"layout_next", SH_LAYOUT_NEXT},
        {"layout_prev", SH_LAYOUT_PREV},
        {"layout_dwindle", SH_SET_LAYOUT_DWINDLE},
        {"layout_master", SH_SET_LAYOUT_MASTER},
        {"layout_spiral", SH_SET_LAYOUT_SPIRAL},
        {"layout_monocle", SH_SET_LAYOUT_MONOCLE},
        {"layout_scroll", SH_SET_LAYOUT_SCROLL},
        {"promote", SH_PROMOTE},
        {"focus_next", SH_FOCUS_NEXT},
        {"focus_prev", SH_FOCUS_PREV},
        {"swap_next", SH_SWAP_NEXT},
        {"swap_prev", SH_SWAP_PREV},
        {"master_grow", SH_MASTER_GROW},
        {"master_shrink", SH_MASTER_SHRINK},
        {"master_more", SH_MASTER_MORE},
        {"master_less", SH_MASTER_LESS},
        {"split_toggle", SH_SPLIT_TOGGLE},
        {"split_swap", SH_SPLIT_SWAP},
        {"pseudo_toggle", SH_PSEUDO_TOGGLE},
        {"peek", SH_PEEK},
        {"peek_toggle", SH_PEEK_TOGGLE},
        {"night_light_toggle", SH_NIGHT_LIGHT_TOGGLE},
        {"night_light_on", SH_NIGHT_LIGHT_ON},
        {"night_light_off", SH_NIGHT_LIGHT_OFF},
        {"night_light_auto", SH_NIGHT_LIGHT_AUTO},
        {"profile_auto", SH_PROFILE_AUTO},
        {"keep_awake_toggle", SH_KEEP_AWAKE_TOGGLE},
        {"keep_awake_on", SH_KEEP_AWAKE_ON},
        {"keep_awake_off", SH_KEEP_AWAKE_OFF},
        {"zoom_in", SH_ZOOM_IN},
        {"zoom_out", SH_ZOOM_OUT},
        {"zoom_reset", SH_ZOOM_RESET},
        {"move_workspace_to_output", SH_MOVE_WORKSPACE_TO_OUTPUT},
        {"swap_workspaces", SH_SWAP_WORKSPACES},
        {"swallow_toggle", SH_SWALLOW_TOGGLE},
        {"switch_layout", SH_SWITCH_LAYOUT},
        {"dnd_toggle", SH_DND_TOGGLE},
        {"dnd_on", SH_DND_ON},
        {"dnd_off", SH_DND_OFF},
        {"notification_history", SH_NOTIFICATION_HISTORY},
        {"clipboard_history", SH_CLIPBOARD_HISTORY},
        {"emoji_picker", SH_EMOJI_PICKER},
        {"pick_color", SH_PICK_COLOR},
        {"poweroff", SH_POWER_OFF},
        {"reboot", SH_REBOOT},
        {"suspend", SH_SUSPEND},
        {"hibernate", SH_HIBERNATE},
        {"logout", SH_LOGOUT},
        {"lock", SH_LOCK},
        {"power_menu", SH_POWER_MENU},
        {"display_off", SH_DISPLAY_OFF},
        {"display_on", SH_DISPLAY_ON},
        {"display_toggle", SH_DISPLAY_TOGGLE},
        {"display_mode", SH_DISPLAY_MODE},
        {"display_settings", SH_DISPLAY_SETTINGS},
        {"desktop_clock", SH_DESKTOP_CLOCK},
        {"wallpaper_next", SH_WALLPAPER_NEXT},
        {"wallpaper_prev", SH_WALLPAPER_PREV},
        {"scroll_left", SH_SCROLL_LEFT},
        {"scroll_right", SH_SCROLL_RIGHT},
        {"column_widen", SH_COLUMN_WIDEN},
        {"column_narrow", SH_COLUMN_NARROW},
        {"column_cycle_width", SH_COLUMN_CYCLE_WIDTH},
        {"consume_left", SH_CONSUME_LEFT},
        {"consume_right", SH_CONSUME_RIGHT},
        {"expel", SH_EXPEL},
        {"center_column", SH_CENTER_COLUMN},
        {"toggle_floating", SH_TOGGLE_FLOATING},
        {"launcher", SH_LAUNCHER},
        {"taskbar_focus", SH_TASKBAR_FOCUS},
        {"volume_up", SH_VOLUME_UP},
        {"volume_down", SH_VOLUME_DOWN},
        {"volume_mute", SH_VOLUME_MUTE},
        {"mic_mute", SH_MIC_MUTE},
        {"brightness_up", SH_BRIGHTNESS_UP},
        {"brightness_down", SH_BRIGHTNESS_DOWN},
        {"mode", SH_MODE},
        {"toggle_shortcuts_inhibit", SH_TOGGLE_SHORTCUTS_INHIBIT},
        {"focus_left", SH_FOCUS_LEFT},
        {"focus_right", SH_FOCUS_RIGHT},
        {"focus_up", SH_FOCUS_UP},
        {"focus_down", SH_FOCUS_DOWN},
        {"screenshot", SH_SCREENSHOT},
        {"move_left", SH_MOVE_LEFT},
        {"move_right", SH_MOVE_RIGHT},
        {"move_up", SH_MOVE_UP},
        {"move_down", SH_MOVE_DOWN},
        {"move_to_scratchpad", SH_MOVE_TO_SCRATCHPAD},
        {"scratchpad_show", SH_SCRATCHPAD_SHOW},
        {"toggle_sticky", SH_TOGGLE_STICKY},
        {"toggle_above", SH_TOGGLE_ABOVE},
        {"resize_left", SH_RESIZE_LEFT},
        {"resize_right", SH_RESIZE_RIGHT},
        {"resize_up", SH_RESIZE_UP},
        {"resize_down", SH_RESIZE_DOWN},
        {"switcher", SH_SWITCHER_NEXT},
        {"switcher_prev", SH_SWITCHER_PREV},
        {"switcher_app", SH_SWITCHER_APP_NEXT},
        {"switcher_app_prev", SH_SWITCHER_APP_PREV},
        {"switcher_confirm", SH_SWITCHER_CONFIRM},
        {"switcher_cancel", SH_SWITCHER_CANCEL},
        {"focus_last", SH_FOCUS_LAST},
        {"focus_urgent", SH_FOCUS_URGENT},
        {"group_toggle", SH_GROUP_TOGGLE},
        {"group_next", SH_GROUP_NEXT},
        {"group_prev", SH_GROUP_PREV},
        {"ungroup", SH_UNGROUP},
        {"group_merge_left", SH_GROUP_MERGE_LEFT},
        {"group_merge_right", SH_GROUP_MERGE_RIGHT},
        {"group_merge_up", SH_GROUP_MERGE_UP},
        {"group_merge_down", SH_GROUP_MERGE_DOWN},
        {"palette", SH_PALETTE},
        {"shortcuts", SH_SHORTCUTS},
        {"toggle_overview", SH_OVERVIEW_TOGGLE},
        {"overview_confirm", SH_OVERVIEW_CONFIRM},
        {"overview_cancel", SH_OVERVIEW_CANCEL},
        {"media_play_pause", SH_MEDIA_PLAY_PAUSE},
        {"media_next", SH_MEDIA_NEXT},
        {"media_previous", SH_MEDIA_PREVIOUS},
        {"media_stop", SH_MEDIA_STOP},
        {"shake", SH_SHAKE},
        {"gravity", SH_GRAVITY},
        {"party", SH_PARTY},
        {"cat", SH_CAT},
        {"ambient", SH_AMBIENT},
        {"cursor_trail", SH_CURSOR_TRAIL},
        {"xray", SH_XRAY},
        {"heatmap", SH_HEATMAP},
};
} // namespace

const std::vector<std::string> &tiling_bar_modules() {
    static const std::vector<std::string> modules{
        "launcher", "workspaces", "layout",        "mode",           "window",   "clock",
        "tray",     "keyboard",   "network",       "volume",         "battery",  "notifications",
        "tiling",   "profiles",   "wallpapers",    "quick_settings", "power"};
    return modules;
}

std::vector<std::string> config_action_names() {
    std::vector<std::string> names;
    for (const auto &entry : action_table)
        names.emplace_back(entry.first);
    return names;
}

std::string join(std::span<const std::string> words, std::string_view separator) {
    std::string result;
    for (size_t i = 0; i < words.size(); ++i)
        result += (i ? std::string(separator) : "") + words[i];
    return result;
}

sh_action parse_action(const std::string &name) {
    for (const auto &[candidate, action] : action_table)
        if (name == candidate)
            return action;
    unknown("action", name, config_action_names(), "=" + name);
}

int Config::workspace_number(const std::string &name) const {
    if (name.empty())
        return 0;
    for (std::size_t i = 0; i < workspace_names.size(); ++i)
        if (workspace_names[i] == name)
            return static_cast<int>(i) + 1;
    std::size_t used = 0;
    try {
        int number = std::stoi(name, &used);
        if (used == name.size() && number >= 1 && number <= settings.workspaces)
            return number;
    } catch (const std::logic_error &) {
    }
    return 0;
}

std::string Config::workspace_label(int workspace) const {
    std::string label = std::to_string(workspace);
    if (workspace >= 1 && static_cast<std::size_t>(workspace) <= workspace_names.size() &&
        !workspace_names[static_cast<std::size_t>(workspace) - 1].empty())
        label += " " + workspace_names[static_cast<std::size_t>(workspace) - 1];
    return label;
}

bool action_takes_workspace(sh_action action) {
    return action == SH_WORKSPACE || action == SH_MOVE_TO_WORKSPACE;
}

bool action_takes_output(sh_action action) {
    return action == SH_MOVE_WORKSPACE_TO_OUTPUT || action == SH_SWAP_WORKSPACES;
}

bool valid_output_target(const std::string &target) {
    if (target.empty() || target.size() > 63)
        return false;
    for (unsigned char c : target)
        if (c < 0x20 || c == 0x7f)
            return false;
    return target != "desc:";
}

bool action_takes_display(sh_action action) {
    return action == SH_DISPLAY_OFF || action == SH_DISPLAY_ON || action == SH_DISPLAY_TOGGLE;
}

bool action_takes_amount(sh_action action) {
    return action == SH_RESIZE_LEFT || action == SH_RESIZE_RIGHT || action == SH_RESIZE_UP ||
           action == SH_RESIZE_DOWN || action == SH_VOLUME_UP || action == SH_VOLUME_DOWN ||
           action == SH_BRIGHTNESS_UP || action == SH_BRIGHTNESS_DOWN;
}

namespace {
bool takes_percent(sh_action action) {
    return action == SH_VOLUME_UP || action == SH_VOLUME_DOWN || action == SH_BRIGHTNESS_UP ||
           action == SH_BRIGHTNESS_DOWN;
}
} // namespace

int default_amount(sh_action action) {
    return takes_percent(action) ? default_step_percent : default_resize_amount;
}

int max_amount(sh_action action) {
    return takes_percent(action) ? 100 : max_resize_amount;
}

int parse_layout_choice(const std::string &word) {
    if (word == "next")
        return 0;
    if (word == "prev")
        return -1;
    std::size_t used = 0;
    int number = 0;
    try {
        number = std::stoi(word, &used);
    } catch (const std::logic_error &) {
    }
    if (used != word.size() || number < 1 || number > max_layouts)
        fail("switch_layout takes \"next\", \"prev\", or a layout's number from 1 to " +
                 std::to_string(max_layouts) + ", not '" + word + "'",
             "layout");
    return number;
}

sh_display_mode parse_display_mode(const std::string &name) {
    static constexpr std::pair<std::string_view, sh_display_mode> modes[] = {
        {"extend", SH_DISPLAY_MODE_EXTEND},
        {"duplicate", SH_DISPLAY_MODE_DUPLICATE},
        {"internal", SH_DISPLAY_MODE_INTERNAL},
        {"external", SH_DISPLAY_MODE_EXTERNAL},
    };
    for (const auto &[text, mode] : modes)
        if (name == text)
            return mode;
    fail("display mode must be \"extend\", \"duplicate\", \"internal\" or \"external\"");
}

sh_cat_switch parse_cat_switch(const std::string &name) {
    for (auto [candidate, choice] :
         {std::pair{"toggle", SH_CAT_TOGGLE}, {"on", SH_CAT_ON}, {"off", SH_CAT_OFF}})
        if (name == candidate)
            return choice;
    fail("cat takes \"on\", \"off\" or \"toggle\"");
}

static_assert(SH_AMBIENT_ON_SAKURA == SH_AMBIENT_OFF + int{SH_AMBIENT_SAKURA} &&
              SH_AMBIENT_ON_SNOW == SH_AMBIENT_OFF + int{SH_AMBIENT_SNOW} &&
              SH_AMBIENT_ON_FIREFLIES == SH_AMBIENT_OFF + int{SH_AMBIENT_FIREFLIES} &&
              SH_AMBIENT_ON_EMBERS == SH_AMBIENT_OFF + int{SH_AMBIENT_EMBERS} &&
              SH_AMBIENT_ON_RAIN == SH_AMBIENT_OFF + int{SH_AMBIENT_RAIN});
sh_ambient_choice parse_ambient_choice(const std::string &name) {
    for (auto [candidate, choice] :
         {std::pair{"toggle", SH_AMBIENT_TOGGLE},
          {"next", SH_AMBIENT_NEXT},
          {"off", SH_AMBIENT_OFF},
          {"sakura", SH_AMBIENT_ON_SAKURA},
          {"snow", SH_AMBIENT_ON_SNOW},
          {"fireflies", SH_AMBIENT_ON_FIREFLIES},
          {"embers", SH_AMBIENT_ON_EMBERS},
          {"rain", SH_AMBIENT_ON_RAIN}})
        if (name == candidate)
            return choice;
    fail("ambient takes \"sakura\", \"snow\", \"fireflies\", \"embers\", \"rain\", "
         "\"off\", \"next\" or \"toggle\"");
}

static_assert(SH_CURSOR_TRAIL_ON_SPARKLES == SH_CURSOR_TRAIL_OFF + int{SH_TRAIL_SPARKLES} &&
              SH_CURSOR_TRAIL_ON_PETALS == SH_CURSOR_TRAIL_OFF + int{SH_TRAIL_PETALS} &&
              SH_CURSOR_TRAIL_ON_HEARTS == SH_CURSOR_TRAIL_OFF + int{SH_TRAIL_HEARTS});
sh_cursor_trail_choice parse_cursor_trail_choice(const std::string &name) {
    for (auto [candidate, choice] :
         {std::pair{"toggle", SH_CURSOR_TRAIL_TOGGLE},
          {"next", SH_CURSOR_TRAIL_NEXT},
          {"off", SH_CURSOR_TRAIL_OFF},
          {"sparkles", SH_CURSOR_TRAIL_ON_SPARKLES},
          {"petals", SH_CURSOR_TRAIL_ON_PETALS},
          {"hearts", SH_CURSOR_TRAIL_ON_HEARTS}})
        if (name == candidate)
            return choice;
    fail("cursor_trail takes \"sparkles\", \"petals\", \"hearts\", \"off\", \"next\" or "
         "\"toggle\"");
}

sh_screenshot_mode parse_screenshot_mode(const std::string &name) {
    for (auto [candidate, mode] : {std::pair{"region", SH_SCREENSHOT_REGION},
                                   {"output", SH_SCREENSHOT_OUTPUT},
                                   {"window", SH_SCREENSHOT_WINDOW}})
        if (name == candidate)
            return mode;
    fail("screenshot mode must be \"region\", \"output\", or \"window\"");
}

sh_heatmap_choice parse_heatmap_choice(const std::string &name) {
    for (auto [candidate, choice] :
         {std::pair{"toggle", SH_HEATMAP_TOGGLE}, {"motion", SH_HEATMAP_MOTION},
          {"clicks", SH_HEATMAP_CLICKS}, {"reset", SH_HEATMAP_RESET}})
        if (name == candidate)
            return choice;
    fail("heatmap must be \"toggle\", \"motion\", \"clicks\" or \"reset\"");
}

namespace {
// Whether `pattern`, from compile_pattern, is found in `text`. Of a text longer than twice
// `window` bytes only the first and the last `window` are searched: a client may make its title
// as long as it likes, and no rule needs more of it. `^` matches only at the start of the first
// part, `$` only at the end of the last, and `\b` there sees the byte before it.
bool pattern_found(const std::regex &pattern, const std::string &text) {
    using namespace std::regex_constants;
    constexpr std::ptrdiff_t window = 512;
    if (text.size() <= 2 * static_cast<size_t>(window))
        return std::regex_search(text, pattern, match_continuous);
    return std::regex_search(text.begin(), text.begin() + window, pattern,
                             match_continuous | match_not_eol) ||
           std::regex_search(text.end() - window, text.end(), pattern,
                             match_continuous | match_prev_avail);
}
} // namespace

bool WindowRule::matches(const std::string &app_id, const std::string &title) const {
    return (!pattern || pattern_found(*pattern, app_id)) &&
           (!title_pattern || pattern_found(*title_pattern, title));
}

bool WindowActions::empty() const {
    return !floating && !fullscreen && !maximize && !focus && !sticky && !above && !allow_tearing &&
           !blur && !scratchpad && !shadow && !border && !corner_radius && !border_color &&
           !border_inactive_color && !idle_inhibit &&
           !workspace && !output && !size && position == Position::Unset && !shortcuts_inhibit;
}

void WindowActions::merge(const WindowActions &other) {
    for (auto [target, source] : {std::pair{&floating, &other.floating},
                                  {&fullscreen, &other.fullscreen},
                                  {&maximize, &other.maximize},
                                  {&focus, &other.focus},
                                  {&sticky, &other.sticky},
                                  {&shortcuts_inhibit, &other.shortcuts_inhibit},
                                  {&above, &other.above},
                                  {&allow_tearing, &other.allow_tearing},
                                  {&blur, &other.blur},
                                  {&scratchpad, &other.scratchpad},
                                  {&shadow, &other.shadow},
                                  {&border, &other.border}})
        if (*source)
            *target = *source;
    for (auto [target, source] : {std::pair{&border_color, &other.border_color},
                                  {&border_inactive_color, &other.border_inactive_color}})
        if (*source)
            *target = *source;
    if (other.corner_radius)
        corner_radius = other.corner_radius;
    if (other.idle_inhibit)
        idle_inhibit = other.idle_inhibit;
    if (other.workspace)
        workspace = other.workspace;
    if (other.output)
        output = other.output;
    if (other.size)
        size = other.size;
    if (other.position != Position::Unset) {
        position = other.position;
        x = other.x;
        y = other.y;
    }
}

sh_window_rule WindowActions::to_c() const {
    sh_window_rule rule{};
    rule.floating = floating ? *floating : -1;
    rule.workspace = workspace.value_or(0);
    if (output)
        copy_text(*output, rule.output, "window rule output");
    if (size)
        std::tie(rule.width, rule.height) = *size;
    rule.position = position == Position::Center ? SH_RULE_POSITION_CENTER
                    : position == Position::At   ? SH_RULE_POSITION_AT
                                                 : SH_RULE_POSITION_UNSET;
    rule.x = x;
    rule.y = y;
    rule.fullscreen = fullscreen.value_or(false);
    rule.maximize = maximize.value_or(false);
    rule.no_focus = !focus.value_or(true);
    rule.sticky = sticky.value_or(false);
    rule.no_shortcuts_inhibit = !shortcuts_inhibit.value_or(true);
    rule.above = above.value_or(false);
    rule.allow_tearing = allow_tearing.value_or(false);
    rule.blur = blur ? *blur : -1;
    rule.scratchpad = scratchpad.value_or(false);
    rule.shadow = shadow ? *shadow : -1;
    rule.border = border ? *border : -1;
    rule.corner_radius = corner_radius.value_or(-1);
    if (border_color)
        rule.border_color = *border_color;
    if (border_inactive_color)
        rule.border_inactive_color = *border_inactive_color;
    rule.idle_inhibit = idle_inhibit.value_or(SH_IDLE_RULE_UNSET);
    return rule;
}

WindowActions WindowActions::held() const {
    WindowActions result;
    result.floating = floating;
    result.sticky = sticky;
    result.above = above;
    return result;
}

WindowActions WindowActions::once() const {
    WindowActions result = *this;
    result.floating = result.sticky = result.above = std::nullopt;
    return result;
}

float Config::window_opacity(const std::string &app_id, const std::string &title,
                             bool active) const {
    for (const auto &rule : window_rules)
        if (rule.sets_opacity && rule.matches(app_id, title))
            return active ? rule.opacity : rule.inactive_opacity;
    return active ? opacity : inactive_opacity;
}

WindowActions Config::window_actions(const std::string &app_id, const std::string &title) const {
    WindowActions result;
    if (!settings.window_rules)
        return result;
    for (const auto &rule : window_rules)
        if (!rule.actions.empty() && rule.matches(app_id, title))
            result.merge(rule.dynamic ? rule.actions.once() : rule.actions);
    return result;
}

WindowActions Config::dynamic_actions(const std::string &app_id, const std::string &title) const {
    WindowActions result;
    if (!settings.window_rules)
        return result;
    for (const auto &rule : window_rules)
        if (rule.dynamic && !rule.actions.held().empty() && rule.matches(app_id, title))
            result.merge(rule.actions.held());
    return result;
}

const Binding *Config::binding(uint32_t modifiers, uint32_t keysym, bool release) const {
    return find_key(bindings, modifiers, keysym, release);
}

const Binding *Config::mode_binding(int mode, uint32_t modifiers, uint32_t keysym,
                                    bool release) const {
    if (mode < 1 || static_cast<std::size_t>(mode) > modes.size())
        return binding(modifiers, keysym, release);
    return find_key(modes[static_cast<std::size_t>(mode) - 1].bindings, modifiers, keysym,
                    release);
}

int Config::mode_number(const std::string &name) const {
    if (name == "default")
        return 0;
    for (std::size_t i = 0; i < modes.size(); ++i)
        if (modes[i].name == name)
            return static_cast<int>(i) + 1;
    return -1;
}

std::vector<std::string> Config::mode_names() const {
    std::vector<std::string> names{"default"};
    for (const auto &mode : modes)
        names.push_back(mode.name);
    return names;
}

std::string Config::binding_keys(sh_action action) const {
    for (const auto &binding : bindings) {
        char name[64];
        if (binding.action != action || binding.button || binding.switch_type >= 0 ||
            xkb_keysym_get_name(binding.keysym, name, sizeof(name)) <= 0)
            continue;
        std::string keys;
        for (auto [bit, modifier] : {std::pair{SH_LOGO, "Super"}, {SH_CTRL, "Ctrl"},
                                     {SH_ALT, "Alt"}, {SH_SHIFT, "Shift"}})
            if (binding.modifiers & bit)
                keys += std::string(modifier) + " + ";
        // A letter as printed on the key.
        if (!name[1])
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        return keys + name;
    }
    return {};
}

const Binding *Config::switch_binding(sh_switch type, bool on) const {
    return find_switch(bindings, type, on);
}

const Binding *Config::button_binding(uint32_t modifiers, uint32_t button,
                                      sh_pointer_target target, const std::string &app_id) const {
    constexpr uint32_t relevant = SH_SHIFT | SH_CTRL | SH_ALT | SH_LOGO;
    modifiers &= relevant;
    for (const auto &binding : bindings) {
        if (!binding.button || binding.button != button || binding.modifiers != modifiers)
            continue;
        bool anywhere = !binding.pattern && !binding.desktop;
        if (anywhere || (binding.desktop && target == SH_POINTER_DESKTOP) ||
            (binding.pattern && target == SH_POINTER_WINDOW &&
             pattern_found(*binding.pattern, app_id)))
            return binding.action == SH_NONE ? nullptr : &binding;
    }
    return nullptr;
}

namespace {
int panic(lua_State *L) {
    const char *message = lua_tostring(L, -1);
    std::fprintf(stderr, "paw: unprotected error in the configuration's Lua: %s\n",
                 message ? message : "not a string");
    return 0; // Lua aborts
}
State sandbox() {
    auto memory = std::make_unique<Memory>();
    State state(lua_newstate(allocate, memory.get()));
    if (!state)
        fail("cannot allocate Lua state");
    memory.release();
    auto *L = state.get();
    lua_atpanic(L, panic);
    // Configuration can compute values but cannot perform I/O or launch processes.
    const std::pair<const char *, lua_CFunction> libraries[] = {{"_G", luaopen_base},
                                                                {LUA_TABLIBNAME, luaopen_table},
                                                                {LUA_STRLIBNAME, luaopen_string},
                                                                {LUA_MATHLIBNAME, luaopen_math},
                                                                {LUA_UTF8LIBNAME, luaopen_utf8}};
    for (const auto &[name, open] : libraries) {
        luaL_requiref(L, name, open, 1);
        lua_pop(L, 1);
    }
    for (const char *name : {"dofile", "loadfile", "load", "print", "collectgarbage",
                             "setmetatable", "getmetatable"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_sethook(L, instruction_limit, LUA_MASKCOUNT, 1000);
    return state;
}
// Runs one chunk and leaves its single result on the stack.
void evaluate(lua_State *L, const std::string &source, const std::string &name) {
    *static_cast<int *>(lua_getextraspace(L)) = 1000;
    lua_sethook(L, instruction_limit, LUA_MASKCOUNT, 1000);
    // Only the configuration's code is held to the memory limit: reading what it returned
    // happens outside a protected call, where running out would abort.
    auto &memory = memory_of(L);
    memory.limited = true;
    memory.refused = false;
    int status = luaL_loadbufferx(L, source.data(), source.size(), name.c_str(), "t");
    if (status == LUA_OK)
        status = lua_pcall(L, 0, 1, 0);
    memory.limited = false;
    const char *message = status == LUA_OK ? nullptr : lua_tostring(L, -1);
    // A full string buffer raises its "not enough memory" as an ordinary error.
    if (memory.refused &&
        (status == LUA_ERRMEM || (message && std::string_view(message) == "not enough memory")))
        fail((name.starts_with('@') ? name.substr(1) : name) + ": needs more than " +
             std::to_string(memory_limit >> 20) + " MiB of memory");
    if (status != LUA_OK)
        fail(message ? message : "Lua raised a non-string error");
}
std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        fail("cannot open " + path.string());
    std::string source;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
        source.append(buffer, static_cast<size_t>(file.gcount()));
        if (source.size() > 1024 * 1024)
            fail("file exceeds 1 MiB");
    }
    if (file.bad())
        fail("cannot read " + path.string());
    return source;
}
bool is_record(lua_State *L, int index) {
    return lua_istable(L, index) && lua_rawlen(L, index) == 0;
}
// Lays the table at `source` over the one at `target`, descending into records both hold. With
// `replace` its values replace those of `target`; without, they only fill in what `target` lacks,
// and lists and values it sets stay as they are. `what` names the tables nested too deeply.
void overlay(lua_State *L, int target, int source, bool replace, const char *what, int depth = 0) {
    // Each level holds a key, its value and the target's on the stack, and two more to set one:
    // Lua grows its stack for C code only when asked.
    if (depth > 16 || !lua_checkstack(L, 5))
        fail(std::string(what) + " tables are nested too deeply");
    target = lua_absindex(L, target);
    source = lua_absindex(L, source);
    lua_pushnil(L);
    while (lua_next(L, source)) {
        lua_pushvalue(L, -2);
        lua_rawget(L, target);
        if (is_record(L, -1) && is_record(L, -2)) {
            overlay(L, -1, -2, replace, what, depth + 1);
        } else if (replace || lua_isnil(L, -1)) {
            lua_pushvalue(L, -3);
            lua_pushvalue(L, -3);
            lua_rawset(L, target);
        }
        lua_pop(L, 2);
    }
}
// Pushes `data`, a rice's, as a Lua value.
void push_data(lua_State *L, const RiceData &data) {
    switch (data.type) {
    case RiceData::Type::Boolean: lua_pushboolean(L, data.boolean); return;
    case RiceData::Type::Integer: lua_pushinteger(L, data.integer); return;
    case RiceData::Type::Number: lua_pushnumber(L, data.number); return;
    case RiceData::Type::String: lua_pushlstring(L, data.string.data(), data.string.size()); return;
    case RiceData::Type::Table: break;
    }
    luaL_checkstack(L, 3, "rice tables are nested too deeply");
    lua_createtable(L, static_cast<int>(data.items.size()), static_cast<int>(data.fields.size()));
    for (size_t i = 0; i < data.items.size(); ++i) {
        push_data(L, data.items[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    for (const auto &[key, value] : data.fields) {
        push_data(L, value);
        lua_setfield(L, -2, key.c_str());
    }
}
// The value at `index` as a rice holds it: a table's list, then its names in order.
RiceData data_of(lua_State *L, int index, const std::string &what, int depth = 0) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: return RiceData::boolean_of(lua_toboolean(L, index));
    case LUA_TNUMBER:
        if (lua_isinteger(L, index))
            return RiceData::integer_of(lua_tointeger(L, index));
        else {
            RiceData number;
            number.type = RiceData::Type::Number;
            number.number = lua_tonumber(L, index);
            return number;
        }
    case LUA_TSTRING: {
        size_t length = 0;
        const char *text = lua_tolstring(L, index, &length);
        return RiceData::string_of(std::string(text, length));
    }
    case LUA_TTABLE: break;
    default: fail(what + " is " + described(L, index) + ", which a rice cannot hold");
    }
    if (depth > 16 || !lua_checkstack(L, 2)) // a key and its value at each level
        fail(what + " is nested too deeply");
    RiceData table;
    auto length = static_cast<lua_Integer>(lua_rawlen(L, index));
    for (lua_Integer i = 1; i <= length; ++i) {
        lua_rawgeti(L, index, i);
        table.items.push_back(data_of(L, -1, what, depth + 1));
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    while (lua_next(L, index)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::string key = lua_tostring(L, -2);
            table.fields.emplace_back(key, data_of(L, -1, what + "." + key, depth + 1));
        } else if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 ||
                   lua_tointeger(L, -2) > length) {
            fail(what + " has a key a rice cannot hold");
        }
        lua_pop(L, 1);
    }
    std::sort(table.fields.begin(), table.fields.end(),
              [](const auto &a, const auto &b) { return a.key < b.key; });
    return table;
}
std::filesystem::path theme_path(lua_State *L, const std::filesystem::path &directory) {
    auto name = text(L, "theme");
    return name ? directory / *name : std::filesystem::path();
}
// `theme = "theme.lua"` fills in every setting the configuration leaves out. A missing theme
// file is not an error, so a configuration can name one before `paw import` writes it.
void include_theme(lua_State *L, const std::filesystem::path &directory) {
    table(L, -1, "configuration result");
    auto path = theme_path(L, directory);
    if (path.empty() || !std::filesystem::exists(path))
        return;
    evaluate(L, read_file(path), "@" + path.string());
    table(L, -1, "theme result");
    lua_getfield(L, -1, "theme");
    if (!lua_isnil(L, -1))
        fail("a theme cannot include another theme");
    lua_pop(L, 1);
    overlay(L, -2, -1, false, "theme");
    lua_pop(L, 1);
}
} // namespace
std::filesystem::path default_config_path() {
    const auto *variable = std::getenv("PAW_DEFAULT_CONFIG");
    return variable && *variable ? variable : PAW_DEFAULT_CONFIG;
}
namespace {
// Makes the string at `key` of the table on top of the stack absolute when it names a file
// relative to `directory`; with `home`, one in the home folder ("~/...") stays as it is.
void anchor_file(lua_State *L, const char *key, const std::filesystem::path &directory,
                 bool home = false) {
    lua_getfield(L, -1, key);
    if (lua_type(L, -1) == LUA_TSTRING) {
        std::filesystem::path file = lua_tostring(L, -1);
        if (!file.empty() && file.is_relative() && !(home && file.string().starts_with('~'))) {
            lua_pushstring(L, (directory / file).c_str());
            lua_setfield(L, -3, key);
        }
    }
    lua_pop(L, 1);
}
// Makes `shell.wallpaper`, `shell.monitor_wallpapers`' pictures, `shell.start_icon` and
// `shell.wallpaper_cycle.folder` in the table at `index` absolute when they name a file relative
// to `directory`.
void anchor_files(lua_State *L, int index, const std::filesystem::path &directory) {
    lua_getfield(L, index, "shell");
    if (lua_istable(L, -1)) {
        for (const char *key : {"wallpaper", "start_icon"})
            anchor_file(L, key, directory);
        lua_getfield(L, -1, "wallpaper_cycle");
        if (lua_istable(L, -1))
            anchor_file(L, "folder", directory, true);
        lua_pop(L, 1);
        lua_getfield(L, -1, "monitor_wallpapers");
        if (lua_istable(L, -1)) {
            std::vector<std::string> names;
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                if (lua_type(L, -2) == LUA_TSTRING)
                    names.emplace_back(lua_tostring(L, -2));
                lua_pop(L, 1);
            }
            for (const auto &name : names)
                anchor_file(L, name.c_str(), directory);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}
// The shipped configuration's wallpapers and start buttons, its profiles' too, may be named
// relative to it, as any configuration's are; made absolute here, they stay beside it rather
// than being looked for beside the configuration that extends it.
void anchor_shipped_files(lua_State *L, const std::filesystem::path &directory) {
    int index = lua_absindex(L, -1);
    anchor_files(L, index, directory);
    lua_getfield(L, index, "profiles");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_istable(L, -1))
                anchor_files(L, lua_absindex(L, -1), directory);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}
// `extends = "default"` layers the configuration, theme included, over the shipped default
// configuration: every setting it leaves out comes from there, its bindings go first, and the
// defaults fill in the keys it does not bind. Returns how many bindings are its own.
size_t include_defaults(lua_State *L) {
    lua_getfield(L, -1, "extends");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return SIZE_MAX;
    }
    if (string(L, -1, "extends") != "default")
        fail("extends must be \"default\"");
    lua_pop(L, 1);
    auto path = default_config_path();
    evaluate(L, read_file(path), "@" + path.string());
    table(L, -1, "default configuration result");
    lua_getfield(L, -1, "extends");
    if (!lua_isnil(L, -1))
        fail("the default configuration cannot extend another");
    lua_pop(L, 1);
    anchor_shipped_files(L, std::filesystem::absolute(path).parent_path());
    lua_pushnil(L);
    lua_setfield(L, -2, "theme");
    // A configuration with profiles of its own offers those alone, not the shipped ones too.
    lua_getfield(L, -2, "profiles");
    if (!lua_isnil(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -3, "profiles");
        lua_pushnil(L);
        lua_setfield(L, -3, "profile");
    }
    lua_pop(L, 1);
    lua_getfield(L, -2, "bindings");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -4, "bindings");
    }
    table(L, -1, "bindings");
    size_t own = lua_rawlen(L, -1);
    lua_getfield(L, -2, "bindings");
    if (!lua_isnil(L, -1)) {
        table(L, -1, "default bindings");
        for (lua_Integer i = 1; i <= static_cast<lua_Integer>(lua_rawlen(L, -1)); ++i) {
            lua_rawgeti(L, -1, i);
            lua_rawseti(L, -3, static_cast<lua_Integer>(own) + i);
        }
    }
    lua_pop(L, 2);
    overlay(L, -2, -1, false, "theme");
    lua_pop(L, 1);
    return own;
}
// Sections a profile may set: how the desktop looks, not how it behaves.
const std::vector<std::string> profile_sections = {"appearance", "windows", "shell"};
constexpr size_t max_profiles = 32;
// The names in `profiles`, sorted, after checking its shape.
std::vector<std::string> profile_names(lua_State *L) {
    std::vector<std::string> names;
    lua_getfield(L, -1, "profiles");
    if (!lua_isnil(L, -1)) {
        Named in("profiles");
        table(L, -1, "profiles");
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) != LUA_TSTRING)
                fail("profiles must be keyed by name");
            std::string name = lua_tostring(L, -2);
            // "next" and "prev" mean the neighbouring profile in `paw msg profile`.
            if (!valid_name(name, {"next", "prev"}))
                fail("profile name '" + name +
                         "' must be 1 to 32 letters, digits, '-' or '_', and not next or prev",
                     name);
            table(L, -1, ("profiles." + name).c_str());
            keys_among(L, -1, profile_sections, "profiles." + name);
            names.push_back(name);
            lua_pop(L, 1);
        }
        if (names.size() > max_profiles)
            fail("at most 32 profiles");
    }
    lua_pop(L, 1);
    std::sort(names.begin(), names.end());
    return names;
}
// `profile`, the profile the configuration starts with; "" for none.
std::string starting_profile(lua_State *L, const std::vector<std::string> &names) {
    auto name = text(L, "profile");
    if (name && names.empty())
        fail("profile '" + *name + "' needs a profiles table that defines it", "profile");
    if (name && std::find(names.begin(), names.end(), *name) == names.end())
        unknown("profile", *name, names, "profile");
    return name.value_or("");
}
void apply_profile(lua_State *L, const std::string &name) {
    lua_getfield(L, -1, "profiles");
    lua_getfield(L, -1, name.c_str());
    overlay(L, -3, -1, true, "profile");
    lua_pop(L, 2);
}
void shadowed(lua_State *L, int config, int theme, const std::string &prefix,
              std::vector<std::string> &result, int depth = 0) {
    // As deep as overlay goes, a key, its value and the configuration's at each level.
    if (depth > 16 || !lua_checkstack(L, 3))
        return;
    config = lua_absindex(L, config);
    theme = lua_absindex(L, theme);
    lua_pushnil(L);
    while (lua_next(L, theme)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            auto name = prefix + lua_tostring(L, -2);
            lua_pushvalue(L, -2);
            lua_rawget(L, config);
            if (is_record(L, -1) && is_record(L, -2))
                shadowed(L, -1, -2, name + ".", result, depth + 1);
            else if (!lua_isnil(L, -1))
                result.push_back(name);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
}
// The line of `source` where the names in `trail` appear as settings, one after the other
// (the section, then the setting): the first line, past the previous name's, that has
// `name =`, `["name"] =`, or `name =` in a quoted key. Comments are skipped. 0 when not found.
size_t locate(const std::string &source, const std::vector<std::string> &trail) {
    if (trail.empty())
        return 0;
    auto is_word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::vector<std::string> lines;
    for (size_t start = 0; start <= source.size();) {
        auto end = source.find('\n', start);
        if (end == std::string::npos)
            end = source.size();
        auto text = source.substr(start, end - start);
        // Drop a trailing comment, unless the marker sits in a string (good enough for keys).
        if (auto comment = text.find("--"); comment != std::string::npos &&
            std::count(text.begin(), text.begin() + static_cast<long>(comment), '"') % 2 == 0 &&
            std::count(text.begin(), text.begin() + static_cast<long>(comment), '\'') % 2 == 0)
            text.resize(comment);
        lines.push_back(std::move(text));
        start = end + 1;
    }
    auto find_in = [&](const std::string &name, size_t from) -> size_t {
        for (size_t i = from; i < lines.size(); ++i) {
            const auto &line = lines[i];
            if (name.starts_with('=')) { // a string value, as "name" or 'name'
                auto word = name.substr(1);
                if (line.find('"' + word + '"') != std::string::npos ||
                    line.find('\'' + word + '\'') != std::string::npos)
                    return i + 1;
                continue;
            }
            for (size_t at = line.find(name); at != std::string::npos;
                 at = line.find(name, at + 1)) {
                bool before = at == 0 || !is_word(line[at - 1]);
                auto after = at + name.size();
                bool bare = after >= line.size() || !is_word(line[after]);
                if (!before || !bare)
                    continue;
                // `name =`, or `["name"] =` / `['name'] =`.
                if (before && at > 0 && (line[at - 1] == '"' || line[at - 1] == '\''))
                    after += after < line.size() ? 1 : 0;
                while (after < line.size() && (line[after] == ' ' || line[after] == ']'))
                    ++after;
                if (after < line.size() && line[after] == '=' &&
                    (after + 1 >= line.size() || line[after + 1] != '='))
                    return i + 1;
            }
        }
        return 0;
    };
    // Sections only narrow the search; the setting itself must be found.
    size_t found = 0;
    for (size_t i = 0; i < trail.size(); ++i) {
        auto next = find_in(trail[i], found ? found - 1 : 0);
        if (i + 1 == trail.size() && !next)
            return 0;
        found = next ? next : found;
    }
    return found;
}

// A configuration loads first in a child process (try_in_child), which throws a
// ConfigTrialError if it runs out of memory, runs over its processor time (or the clock's
// backstop) or crashes there; whatever `load` throws in time is left to running it for real.
// Without a child process to be had, it returns: the configuration then loads unchecked, rather
// than not at all. `name` is the configuration's chunk name.
void try_first(const std::function<void()> &load, const std::string &name) {
    const auto trial = try_in_child(load);
    auto file = name.starts_with('@') ? name.substr(1) : name;
    switch (trial.outcome) {
    case Trial::Finished:
        return;
    case Trial::OutOfMemory:
        throw ConfigTrialError("configuration: " + file + ": ran out of memory");
    case Trial::TooLong:
        throw ConfigTrialError("configuration: " + file + ": did not finish loading within " +
                               std::to_string(trial.seconds) + " seconds" +
                               (trial.processor_time ? " of processor time" : ""));
    case Trial::Crashed:
        break;
    }
    throw ConfigTrialError("configuration: " + file + ": loading it crashed" +
                           (trial.signal.empty() ? std::string() : " (" + trial.signal + ")"));
}
// parse_config, unchecked.
Config parse(const std::string &source, const std::string &name,
             const std::filesystem::path &directory, const std::string &chosen,
             const std::vector<Rice> &rices) {
    current_section.clear();
    try {
        // The configuration with its theme and defaults, then with `profile` laid over them.
        std::vector<std::string> names;
        std::string start;
        // A profile of the configuration's by name, or a rice's settings.
        auto build = [&](const std::string &profile, const RiceData *rice = nullptr) {
            auto state = sandbox();
            auto *L = state.get();
            evaluate(L, source, name);
            include_theme(L, directory);
            auto own = include_defaults(L);
            names = profile_names(L);
            start = starting_profile(L, names);
            if (rice) {
                push_data(L, *rice);
                overlay(L, -2, -1, true, "profile");
                lua_pop(L, 1);
            } else if (!profile.empty()) {
                apply_profile(L, profile);
            }
            return read(L, own, directory);
        };
        auto config = build("");
        // Profiles leave the keyboard alone, so its keymap is compiled once.
        check_keymap(config.settings);
        auto has = [](const std::vector<std::string> &list, const std::string &item) {
            return std::find(list.begin(), list.end(), item) != list.end();
        };
        // The installed rices, checked first: one the configuration refuses is passed over.
        std::vector<std::string> rice_names, rice_problems;
        std::optional<Config> chosen_rice;
        for (const auto &rice : rices) {
            auto rice_name = rice.directory.filename().string();
            if (has(names, rice_name)) {
                rice_problems.push_back(rice_name + ": the configuration has a profile of that "
                                                    "name");
                continue;
            }
            if (names.size() + rice_names.size() >= max_profiles) {
                rice_problems.push_back(rice_name + ": there are 32 profiles already");
                continue;
            }
            try {
                auto profile = rice_profile(rice);
                auto layered = build("", &profile);
                if (rice_name == chosen)
                    chosen_rice = std::move(layered);
                rice_names.push_back(rice_name);
            } catch (const ConfigError &error) {
                rice_problems.push_back(rice_name + ": " + error.text);
            } catch (const std::exception &error) {
                rice_problems.push_back(rice_name + ": " + error.what());
            }
        }
        // Every profile is checked, so picking one later cannot fail; the configuration
        // itself is valid by now, so an error belongs to the profile.
        auto active = has(names, chosen) || chosen_rice ? chosen : start;
        auto all = names;
        for (const auto &profile : all) {
            try {
                auto layered = build(profile);
                if (profile == active)
                    config = std::move(layered);
            } catch (const ConfigError &error) {
                auto trail = error.trail;
                trail.insert(trail.begin(), {"profiles", profile});
                throw ConfigError("profile '" + profile + "': " + error.text, trail);
            }
        }
        if (chosen_rice && active == chosen)
            config = std::move(*chosen_rice);
        all.insert(all.end(), rice_names.begin(), rice_names.end());
        std::sort(all.begin(), all.end());
        // The schedule's profiles, an installed rice's among them.
        const auto &schedule = config.settings.profile_schedule;
        for (const char *key : {"day", "night"}) {
            std::string profile = key[0] == 'd' ? schedule.day : schedule.night;
            if (profile.empty() || has(all, profile))
                continue;
            Named in("profile_schedule");
            if (all.empty())
                fail("profile_schedule." + std::string(key) + " '" + profile +
                         "' needs a profiles table that defines it",
                     key);
            unknown("profile", profile, all, key, "profile_schedule." + std::string(key));
        }
        config.profiles = std::move(all);
        config.rices = std::move(rice_names);
        config.rice_problems = std::move(rice_problems);
        config.profile = active;
        copy_text(active, config.settings.profile, "profile");
        return config;
    } catch (const ConfigError &error) {
        auto line = locate(source, error.trail);
        if (!line)
            throw;
        auto file = name.starts_with('@') ? name.substr(1) : name;
        throw std::runtime_error("configuration: " + file + ":" + std::to_string(line) + ": " +
                                 error.text);
    }
}
} // namespace

Config parse_config(const std::string &source, const std::string &name,
                    const std::filesystem::path &directory, const std::string &chosen,
                    const std::vector<Rice> &rices) {
    try_first([&] { (void)parse(source, name, directory, chosen, rices); }, name);
    return parse(source, name, directory, chosen, rices);
}

std::filesystem::path profile_state_path() {
    std::filesystem::path state;
    if (const auto *xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg == '/')
        state = xdg;
    else if (const auto *home = std::getenv("HOME"); home && *home)
        state = std::filesystem::path(home) / ".local/state";
    else
        return {};
    return state / "paw/profile";
}

std::string saved_profile() {
    auto path = profile_state_path();
    std::ifstream file(path);
    std::string name;
    if (!path.empty() && file)
        std::getline(file, name);
    return name;
}

void save_profile(const std::string &name) {
    auto path = profile_state_path();
    if (path.empty())
        throw std::runtime_error("neither XDG_STATE_HOME nor HOME is set");
    std::error_code failure;
    std::filesystem::create_directories(path.parent_path(), failure);
    auto temporary = path;
    temporary += ".new";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << name << '\n';
        if (!file.flush())
            throw std::runtime_error("cannot write " + temporary.string());
    }
    std::filesystem::rename(temporary, path, failure);
    if (failure)
        throw std::runtime_error("cannot write " + path.string() + ": " + failure.message());
}

namespace {
// load_config; `checked`, run first in a child process as parse_config does, else at once.
Config load_from(const std::filesystem::path &path, bool checked) {
    std::vector<std::string> unread;
    auto rices = installed_rices(&unread);
    const auto source = read_file(path);
    const auto name = "@" + path.string();
    auto config = checked ? parse_config(source, name, path.parent_path(), saved_profile(), rices)
                          : parse(source, name, path.parent_path(), saved_profile(), rices);
    config.rice_problems.insert(config.rice_problems.end(), unread.begin(), unread.end());
    return config;
}
} // namespace

Config load_config(const std::filesystem::path &path) { return load_from(path, true); }

Config load_config_or_default(const std::filesystem::path &path, std::string &error,
                              bool reloading) {
    try {
        error.clear();
        return load_config(path);
    } catch (const ConfigTrialError &failure) {
        if (reloading)
            throw;
        error = failure.what();
    } catch (const std::exception &failure) {
        error = failure.what();
    }
    // paw's own files, which need no trial first: a second wait, after the one that may just
    // have run over, would only hold the desktop up longer. (The installed rices layered over
    // them are data, read without running them.)
    for (const std::filesystem::path &fallback : {default_config_path(),
                                                  std::filesystem::path(PAW_SOURCE_CONFIG)}) {
        std::error_code failure;
        if (!std::filesystem::exists(fallback, failure) ||
            std::filesystem::equivalent(fallback, path, failure))
            continue;
        try {
            return load_from(fallback, false);
        } catch (const std::exception &) {
        }
    }
    return Config{};
}

namespace {
std::optional<std::vector<std::string>> list_shadowed(const std::filesystem::path &config) {
    auto state = sandbox();
    auto *L = state.get();
    evaluate(L, read_file(config), "@" + config.string());
    table(L, -1, "configuration result");
    auto path = theme_path(L, config.parent_path());
    if (path.empty())
        return std::nullopt;
    std::vector<std::string> result;
    if (!std::filesystem::exists(path))
        return result;
    evaluate(L, read_file(path), "@" + path.string());
    table(L, -1, "theme result");
    shadowed(L, -2, -1, "", result);
    std::sort(result.begin(), result.end());
    return result;
}
RiceData read_look(const std::filesystem::path &path, const std::string &profile) {
    auto state = sandbox();
    auto *L = state.get();
    current_section.clear();
    evaluate(L, read_file(path), "@" + path.string());
    include_theme(L, path.parent_path());
    include_defaults(L);
    auto names = profile_names(L);
    if (std::find(names.begin(), names.end(), profile) != names.end()) {
        apply_profile(L, profile);
    } else if (!profile.empty()) {
        auto rices = installed_rices();
        auto rice = std::find_if(rices.begin(), rices.end(), [&](const Rice &rice) {
            return rice.directory.filename() == profile;
        });
        if (rice == rices.end())
            throw std::runtime_error("no profile " + profile);
        push_data(L, rice_profile(*rice));
        overlay(L, -2, -1, true, "profile");
        lua_pop(L, 1);
    }
    RiceData look;
    for (const char *section : {"appearance", "windows", "shell"}) {
        lua_getfield(L, -1, section);
        if (lua_istable(L, -1))
            look.fields.emplace_back(section, data_of(L, -1, section));
        lua_pop(L, 1);
    }
    return look;
}
} // namespace

std::optional<std::vector<std::string>> shadowed_settings(const std::filesystem::path &config) {
    try_first([&] { (void)list_shadowed(config); }, "@" + config.string());
    return list_shadowed(config);
}

RiceData configured_look(const std::filesystem::path &path, const std::string &profile) {
    try_first([&] { (void)read_look(path, profile); }, "@" + path.string());
    return read_look(path, profile);
}
} // namespace paw
