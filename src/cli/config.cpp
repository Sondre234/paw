// SPDX-License-Identifier: GPL-3.0-or-later
/* Where the configuration paw starts with lives. */
#include "paw/cli.hpp"
#include "paw/config.hpp"

#include <cstdlib>

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

} // namespace paw::cli
