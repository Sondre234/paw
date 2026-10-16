// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paw/config.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace paw {
// power.lock_style: the stock screen locker, swaylock, dressed in the appearance profile's look
// (its wallpaper, colours and font) rather than swaylock's plain grey, unless the locker is
// another or swaylock has a configuration of its own.

// What the look is made of besides the configuration: what lock_style_context finds on the
// machine, or what a test makes up.
struct LockStyleContext {
    // The configuration file, beside which a relative shell.wallpaper lies.
    std::filesystem::path config_path;
    // $XDG_STATE_HOME/paw/wallpapers as the shell saves it: a line per profile with its name,
    // the wallpaper it configured and the one picked from the panel, by tabs.
    std::string picked_wallpapers;
    // swaylock reads a configuration file of its own, whose look wins.
    bool swaylock_config = false;
    // Whether swaylock can load the picture at a path; one it cannot is left out, and the
    // background colour shows instead. Unset: none can.
    std::function<bool(const std::filesystem::path &)> readable;
};

// power.lock_command's default, { "swaylock", "-f" }.
const Command &stock_lock_command();

// The wallpaper the shell shows for the profile in use (`config.profile`): the one picked for it
// from the panel while the profile still configures the wallpaper it did then, else
// shell.wallpaper, relative to the configuration's folder; empty for none.
std::filesystem::path lock_wallpaper(const Config &config, const std::filesystem::path &config_path,
                                     std::string_view picked_wallpapers);

// The arguments that give swaylock (1.4 and later) the profile's look: the wallpaper filling
// each output over appearance.background, and the indicator, shown while idle too, in the
// shell's panel, accent, text and urgent colours with its font.
Command lock_style_arguments(const Config &config, const LockStyleContext &context);

// The screen locker as `lock` starts it: power.lock_command, followed by lock_style_arguments
// when power.lock_style is on, the command is the stock one and swaylock has no configuration of
// its own. Anything else is left as it is.
Command lock_command(const Config &config, const LockStyleContext &context);

// The context on this machine: the shell's picked wallpapers, and whether
// $XDG_CONFIG_HOME/swaylock/config (or ~/.config's), ~/.swaylock/config or
// /etc/swaylock/config exists, as swaylock looks for them.
LockStyleContext lock_style_context(const std::filesystem::path &config_path);

// `path` escaped for wordexp(3), which swaylock passes its image paths through, so that it comes
// back the same and nothing in it is expanded or run; nothing for a path with a newline, which no
// escape keeps, or a NUL.
std::optional<std::string> wordexp_escape(std::string_view path);
} // namespace paw
