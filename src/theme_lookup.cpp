// SPDX-License-Identifier: GPL-3.0-or-later
// Whether a cursor or icon theme is installed, by its name, where applications look for one.
#include "paw/backend.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::filesystem::path home() {
    const char *home = std::getenv("HOME");
    return home && *home ? home : "/";
}
} // namespace

/* The icons folders of $XDG_DATA_HOME and $XDG_DATA_DIRS, ~/.icons and /usr/share/pixmaps, as
 * GTK, Qt and libXcursor look in them; for a cursor theme $XCURSOR_PATH in their place when it is
 * set, as wlroots and libXcursor do. A cursor theme is installed with its cursors folder or an
 * index.theme (one that only inherits another's cursors), an icon theme with its index.theme. */
extern "C" bool sh_theme_installed(const char *name, bool cursor) {
    const std::string theme = name ? name : "";
    if (theme.empty() || theme.find('/') != std::string::npos || theme == "." || theme == "..")
        return false;
    std::vector<std::filesystem::path> directories;
    auto add = [&](const std::string &list, const char *below) {
        std::istringstream entries(list);
        for (std::string entry; std::getline(entries, entry, ':');)
            if (entry.starts_with("~/"))
                directories.push_back(home() / entry.substr(2) / below);
            else if (entry.starts_with('/'))
                directories.push_back(std::filesystem::path(entry) / below);
    };
    if (const char *path = std::getenv("XCURSOR_PATH"); cursor && path && *path) {
        add(path, "");
    } else {
        const char *data_home = std::getenv("XDG_DATA_HOME"), *data = std::getenv("XDG_DATA_DIRS");
        add(data_home && *data_home ? data_home : "~/.local/share", "icons");
        add("~/.icons", "");
        add(data && *data ? data : "/usr/local/share:/usr/share", "icons");
        add("/usr/share/pixmaps", "");
    }
    std::error_code error;
    for (const auto &directory : directories)
        if ((cursor && std::filesystem::is_directory(directory / theme / "cursors", error)) ||
            std::filesystem::exists(directory / theme / "index.theme", error))
            return true;
    return false;
}
