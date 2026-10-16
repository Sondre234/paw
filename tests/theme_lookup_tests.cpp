// SPDX-License-Identifier: GPL-3.0-or-later
// Whether a cursor or icon theme is installed (src/theme_lookup.cpp), in folders of the test's own.
#include "check.h"
#include "paw/backend.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() / ("paw-theme-lookup-" + std::to_string(getpid()));
    fs::remove_all(root);
    setenv("HOME", (root / "home").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "system").c_str(), 1);
    unsetenv("XCURSOR_PATH");
    // A cursor theme with its cursors, one that only inherits another's, and an icon theme.
    fs::create_directories(root / "data/icons/Bibata/cursors");
    fs::create_directories(root / "home/.icons/Inherits");
    std::ofstream(root / "home/.icons/Inherits/index.theme") << "[Icon Theme]\nInherits=Bibata\n";
    fs::create_directories(root / "system/icons/Papirus");
    std::ofstream(root / "system/icons/Papirus/index.theme") << "[Icon Theme]\nName=Papirus\n";

    CHECK(sh_theme_installed("Bibata", true));
    CHECK(sh_theme_installed("Inherits", true), "a theme that inherits its cursors");
    CHECK(!sh_theme_installed("Bibata", false), "a cursor theme without an index.theme is no icon theme");
    CHECK(sh_theme_installed("Papirus", false) && sh_theme_installed("Inherits", false));
    CHECK(!sh_theme_installed("Missing", true) && !sh_theme_installed("Missing", false));
    for (const char *name : {"", ".", "..", "../data/icons/Bibata", "icons/Papirus"})
        CHECK(!sh_theme_installed(name, true) && !sh_theme_installed(name, false), "'%s'", name);
    CHECK(!sh_theme_installed(nullptr, true));
    // $XCURSOR_PATH takes the folders' place for cursors, not for icons.
    fs::create_directories(root / "cursors/Elsewhere/cursors");
    setenv("XCURSOR_PATH", ("~/nowhere:" + (root / "cursors").string()).c_str(), 1);
    CHECK(sh_theme_installed("Elsewhere", true) && !sh_theme_installed("Bibata", true));
    CHECK(sh_theme_installed("Papirus", false));
    fs::remove_all(root);
    return failures ? 1 : 0;
}
