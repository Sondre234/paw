#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Puts tekketsu (鉄血) on the applications around paw: kitty, fastfetch, Firefox, Discord, Claude
# Code and cdeck, Neovim and Helix, and GTK 4 / libadwaita; with options the fonts, an iron and
# blood icon theme with tekketsu's own icons over it, and a black cursor.
# paw itself needs nothing installed: pick the `tekketsu` profile from the panel's Appearance
# menu, or run `paw msg profile tekketsu`.
#
#   install.sh [options] [parts...]
#
# Parts (all of them when none is named): kitty fastfetch firefox discord claude editors gtk
# wallpapers (tekketsu's ten, where paw's wallpaper picker looks: Pictures/wallpapers/tekketsu,
# or Pictures/tekketsu when there is no wallpapers folder).
# Options:
#   --fonts      also download Chakra Petch, UnifrakturMaguntia, Yuji Syuku, JetBrains Mono
#                and the Nerd Font symbols (OFL/MIT) into ~/.local/share/fonts/paw-tekketsu
#   --icons      also download the Tela icon theme (GPL-3.0) and install it recoloured, iron
#                folders with blood-red symbols, as Tela-tekketsu-black (-light, -dark), and put
#                tekketsu's own icons over it as the Tekketsu theme, in ~/.local/share/icons
#   --cursor     also download the Bibata Modern Classic cursors (GPL-3.0), black with a white
#                edge
#   --no-reload  leave running kittys alone (they reload on their own once restarted)
#   --dry-run    say what would change, change nothing
#   -h, --help   this text
#
# Every file it replaces is kept beside it as FILE.tekketsu-backup (the first time only), and the
# lines it adds to your own files sit between `paw tekketsu` markers, so running it again changes
# nothing new. It never edits paw's configuration, Discord itself, or settings it does not own.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
config=${XDG_CONFIG_HOME:-$HOME/.config}
data=${XDG_DATA_HOME:-$HOME/.local/share}
dry=0 fonts=0 icons=0 cursor=0 reload=1
parts=()

while [ $# -gt 0 ]; do
    case $1 in
        --fonts) fonts=1 ;;
        --icons) icons=1 ;;
        --cursor) cursor=1 ;;
        --no-reload) reload=0 ;;
        --dry-run) dry=1 ;;
        -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        kitty|fastfetch|firefox|discord|claude|editors|gtk|wallpapers) parts+=("$1") ;;
        *) echo "install.sh: unknown option or part: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
[ ${#parts[@]} -gt 0 ] || parts=(kitty fastfetch firefox discord claude editors gtk wallpapers)

say() { printf '  %s\n' "$*"; }
title() { printf '\033[1m%s\033[0m\n' "$*"; }

# Keeps a copy of FILE the first time tekketsu replaces or edits it.
backup() {
    case $1 in */tekketsu/*|*/paw-tekketsu/*) return ;; esac # tekketsu's own folders
    if [ -e "$1" ] && [ ! -e "$1.tekketsu-backup" ]; then
        cp -a "$1" "$1.tekketsu-backup"
        say "kept $1 as $1.tekketsu-backup"
    fi
}

# Copies SOURCE to TARGET (a file), making its directory and keeping what was there.
place() {
    local source=$1 target=$2
    if [ -e "$target" ] && cmp -s "$source" "$target"; then
        say "up to date: $target"
        return
    fi
    if [ $dry = 1 ]; then
        say "would install $target"
        return
    fi
    mkdir -p "$(dirname "$target")"
    backup "$target"
    cp "$source" "$target"
    say "installed $target"
}

# Copies every file under the directory SOURCE into the directory TARGET, as place does; a
# link (an icon another application's name points at) arrives as a copy of what it names.
place_tree() {
    local source=$1 target=$2 file
    while IFS= read -r -d '' file; do
        place "$source/$file" "$target/$file"
    done < <(cd "$source" && find . \( -type f -o -type l \) -printf '%P\0' | sort -z)
}

# Puts BLOCK (lines) into FILE between `paw tekketsu` markers in comment style PREFIX (and SUFFIX,
# for comments that close), replacing the block an earlier run put there; the rest of the file
# stays as it is.
block() {
    local file=$1 prefix=$2 text=$3 suffix=${4:-}
    local begin="$prefix >>> paw tekketsu (rices/tekketsu/install.sh)$suffix" end="$prefix <<< paw tekketsu$suffix"
    if [ -f "$file" ] && grep -qxF "$begin" "$file"; then
        local current
        current=$(awk -v b="$begin" -v e="$end" '$0 == b {on = 1; next} $0 == e {on = 0} on' "$file")
        if [ "$current" = "$text" ]; then
            say "up to date: $file"
            return
        fi
    fi
    if [ $dry = 1 ]; then
        say "would put the tekketsu lines into $file"
        return
    fi
    mkdir -p "$(dirname "$file")"
    backup "$file"
    local rest=""
    if [ -f "$file" ]; then
        rest=$(awk -v b="$begin" -v e="$end" '$0 == b {skip = 1} !skip {print} $0 == e {skip = 0}' "$file")
    fi
    { [ -z "$rest" ] || printf '%s\n' "$rest"; printf '%s\n%s\n%s\n' "$begin" "$text" "$end"; } > "$file.tekketsu-new"
    mv "$file.tekketsu-new" "$file"
    say "updated $file"
}

# Makes `tmp` (the caller's) a temporary directory for downloads, gone when the script ends.
scratch() {
    tmp=$(mktemp -d)
    temporaries+=("$tmp")
}
temporaries=()
cleanup() { local dir; for dir in "${temporaries[@]}"; do rm -rf -- "$dir"; done; }
trap cleanup EXIT

# Downloads URL to FILE.
fetch() {
    if [ $dry = 1 ]; then say "would download $1"; return; fi
    curl -fsSL --retry 2 -o "$2" "$1"
}

# Unpacks the zip ARCHIVE into DIRECTORY, symbolic links (cursor themes are full of them)
# included, and nothing outside DIRECTORY.
unzip_into() {
    python3 -I - "$1" "$2" <<'EOF'
import os, stat, sys, zipfile
archive, root = sys.argv[1], os.path.realpath(sys.argv[2])
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        path = os.path.realpath(os.path.join(root, info.filename))
        if path != root and not path.startswith(root + os.sep):
            continue
        if stat.S_ISLNK(info.external_attr >> 16):
            target = z.read(info).decode()
            if os.path.isabs(target) or ".." in target.split("/"):
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            if os.path.lexists(path):
                os.remove(path)
            os.symlink(target, path)
        elif info.is_dir():
            os.makedirs(path, exist_ok=True)
        else:
            z.extract(info, root)
EOF
}

part_kitty() {
    title "kitty"
    local dir=$config/kitty
    place "$here/kitty/tekketsu.conf" "$dir/tekketsu/tekketsu.conf"
    block "$dir/kitty.conf" "#" "include tekketsu/tekketsu.conf"
    if [ $dry = 0 ] && [ $reload = 1 ] && command -v pkill > /dev/null; then
        pkill -USR1 -x kitty 2> /dev/null && say "asked running kittys to reload" || true
    fi
    say "the glass needs kitty 0.37 or newer (transparent_background_colors)"
}

part_fastfetch() {
    title "fastfetch"
    place "$here/fastfetch/tekketsu.jsonc" "$config/fastfetch/tekketsu.jsonc"
    if [ -f "$here/fastfetch/logo.png" ]; then
        place "$here/fastfetch/logo.png" "$config/fastfetch/tekketsu/logo.png"
    else
        say "no fastfetch/logo.png here: the spec sheet shows without the mage"
    fi
    say "run it with: fastfetch -c tekketsu   (the mage shows in kitty)"
}

# The default profile's directory of a Firefox profiles.ini's folder, if any.
firefox_profile() {
    local root=$1 ini=$1/profiles.ini path=""
    [ -f "$ini" ] || return 1
    # The profile an installation uses ([Install...] Default=), else the one marked Default=1.
    path=$(awk -F= '/^\[Install/{i=1;next} /^\[/{i=0} i&&$1=="Default"{print $2; exit}' "$ini")
    if [ -z "$path" ]; then
        path=$(awk -F= '/^\[Profile/{p="";d=0} $1=="Path"{p=$2} $1=="Default"&&$2=="1"{d=1} d&&p!=""{print p; exit}' "$ini")
    fi
    [ -n "$path" ] || return 1
    case $path in /*) printf '%s\n' "$path" ;; *) printf '%s\n' "$root/$path" ;; esac
}

part_firefox() {
    title "Firefox"
    local root profile found=0
    # The start page finds the wallpaper two folders up, as it does in the rice itself.
    local start=$data/paw-tekketsu/firefox/startpage
    for root in "$HOME/.mozilla/firefox" "$config/mozilla/firefox"; do
        profile=$(firefox_profile "$root") || continue
        [ -d "$profile" ] || continue
        found=1
        say "profile: $profile"
        place_tree "$here/firefox/chrome" "$profile/chrome"
        local prefs
        prefs=$(sed "s|file:///PATH/TO/startpage/index.html|file://$start/index.html|g" "$here/firefox/user.js")
        block "$profile/user.js" "//" "$prefs"
    done
    if [ $found = 0 ]; then
        say "no Firefox profile found (looked for profiles.ini in ~/.mozilla/firefox and ~/.config/mozilla/firefox)"
        return
    fi
    place_tree "$here/firefox/startpage" "$start"
    if [ -d "$here/wallpaper" ]; then
        place_tree "$here/wallpaper" "$data/paw-tekketsu/wallpaper"
    fi
    say "restart Firefox to see it; to make the start page ($start/index.html) home, see user.js"
}

part_discord() {
    title "Discord"
    local found=0 dir
    for dir in "$config/Vencord/themes" "$config/vesktop/themes" "$config/Equicord/themes" \
               "$config/equibop/themes" "$config/BetterDiscord/themes" \
               "$HOME/.var/app/dev.vencord.Vesktop/config/vesktop/themes"; do
        [ -d "$(dirname "$dir")" ] || continue
        found=1
        place "$here/discord/tekketsu.theme.css" "$dir/tekketsu.theme.css"
    done
    if [ $found = 0 ]; then
        say "no Vencord, Vesktop, Equicord, Equibop or BetterDiscord found: Discord itself cannot"
        say "load a theme. With one of them, put discord/tekketsu.theme.css in its themes folder."
    else
        say "turn it on in the client mod's Themes settings, with Discord on a dark theme; for glass,"
        say "turn on window transparency and put  :root { --tekketsu-glass: 1; }  in QuickCSS"
    fi
}

part_claude() {
    title "Claude Code and cdeck"
    local dir=${CLAUDE_CONFIG_DIR:-$HOME/.claude}/themes
    place "$here/claude/tekketsu.json" "$dir/tekketsu.json"
    say "pick it in Claude Code with /theme (custom:tekketsu); cdeck follows"
}

part_editors() {
    title "Neovim and Helix"
    place "$here/editors/nvim/colors/tekketsu.lua" "$config/nvim/colors/tekketsu.lua"
    place "$here/editors/helix/tekketsu.toml" "$config/helix/themes/tekketsu.toml"
    say "Neovim: :colorscheme tekketsu   Helix: theme = \"tekketsu\""
}

part_gtk() {
    title "GTK 4 and libadwaita"
    block "$config/gtk-4.0/gtk.css" "/*" "$(cat "$here/gtk/gtk.css")" " */"
    say "libadwaita applications take it when they next start, while the system prefers dark"
}

# The wallpapers beside the others in the folder paw's wallpaper picker shows when
# shell.wallpapers is unset (Pictures/wallpapers, else Pictures itself, searched with its
# subfolders), in a tekketsu folder of their own; creating a wallpapers folder would move the
# picker away from the pictures it shows now.
part_wallpapers() {
    title "Wallpapers"
    local pictures
    pictures=$(xdg-user-dir PICTURES 2>/dev/null || true)
    [ -n "$pictures" ] && [ "$pictures" != "$HOME" ] || pictures=$HOME/Pictures
    [ -d "$pictures/wallpapers" ] && pictures=$pictures/wallpapers
    place_tree "$here/wallpaper" "$pictures/tekketsu"
    say "the wallpaper picker lists them under tekketsu (unless shell.wallpapers names another folder)"
}

part_fonts() {
    title "Fonts"
    local dir=$data/fonts/paw-tekketsu tmp weight
    scratch
    # Chakra Petch as single weights (it ships no variable font), squared like machined plate,
    # for the panel and the pages; UnifrakturMaguntia, a blackletter, for the few big headings;
    # Yuji Syuku for 鉄血 and the other Japanese in a brush hand; JetBrains Mono for the terminal.
    local fonts=https://raw.githubusercontent.com/google/fonts/690b65d64dabaebf9d18e2d7e8c8e7e02b4cccaf/ofl
    for weight in Light Regular Italic Medium SemiBold Bold BoldItalic; do
        fetch "$fonts/chakrapetch/ChakraPetch-$weight.ttf" "$tmp/ChakraPetch-$weight.ttf"
    done
    fetch "$fonts/chakrapetch/OFL.txt" "$tmp/ChakraPetch-OFL.txt"
    fetch "$fonts/unifrakturmaguntia/UnifrakturMaguntia-Book.ttf" "$tmp/UnifrakturMaguntia-Book.ttf"
    fetch "$fonts/unifrakturmaguntia/OFL.txt" "$tmp/UnifrakturMaguntia-OFL.txt"
    fetch "$fonts/yujisyuku/YujiSyuku-Regular.ttf" "$tmp/YujiSyuku-Regular.ttf"
    fetch "$fonts/yujisyuku/OFL.txt" "$tmp/YujiSyuku-OFL.txt"
    fetch https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip "$tmp/jetbrains.zip"
    fetch https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/NerdFontsSymbolsOnly.tar.xz "$tmp/symbols.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$dir/ChakraPetch" "$dir/UnifrakturMaguntia" "$dir/YujiSyuku" "$dir/JetBrainsMono" \
            "$dir/NerdFontsSymbols"
        cp "$tmp"/ChakraPetch-* "$dir/ChakraPetch/"
        cp "$tmp"/UnifrakturMaguntia-* "$dir/UnifrakturMaguntia/"
        cp "$tmp"/YujiSyuku-* "$dir/YujiSyuku/"
        mkdir -p "$tmp/jetbrains"
        unzip_into "$tmp/jetbrains.zip" "$tmp/jetbrains"
        cp "$tmp"/jetbrains/fonts/ttf/JetBrainsMono-*.ttf "$tmp/jetbrains/OFL.txt" "$dir/JetBrainsMono/"
        tar -xJf "$tmp/symbols.tar.xz" -C "$dir/NerdFontsSymbols"
        fc-cache -f "$dir" > /dev/null 2>&1 || true
        say "installed Chakra Petch, UnifrakturMaguntia, Yuji Syuku, JetBrains Mono and the Nerd Font symbols in $dir"
    fi
}

part_icons() {
    title "Icons"
    local tmp version=2026-07-07
    scratch
    fetch "https://github.com/vinceliuice/Tela-icon-theme/archive/refs/tags/$version.tar.gz" "$tmp/tela.tar.gz"
    if [ $dry = 0 ]; then
        tar -xzf "$tmp/tela.tar.gz" -C "$tmp"
        # Tela's black folders in tekketsu's iron, their symbols in blood.
        sed -i "/^ *black)/,/;;/{s/theme_color='#4d4d4d'/theme_color='#2f2929'/;s/theme_back_color='#ffffff'/theme_back_color='#c42b23'/}" \
            "$tmp/Tela-icon-theme-$version/install.sh"
        (cd "$tmp/Tela-icon-theme-$version" && bash ./install.sh -d "$data/icons" -n Tela-tekketsu black) > /dev/null 2>&1 \
            || { echo "install.sh: Tela's own installer failed" >&2; exit 1; }
        say "installed Tela-tekketsu-black (and -light, -dark) in $data/icons"
    fi
    # tekketsu's own icons over it (they inherit Tela-tekketsu-black-dark for everything else).
    if [ -d "$here/icons/Tekketsu" ]; then
        place_tree "$here/icons/Tekketsu" "$data/icons/Tekketsu"
        if [ $dry = 0 ]; then
            gtk-update-icon-cache -q -t -f "$data/icons/Tekketsu" 2> /dev/null || true
        fi
        say "use it with tekketsu: icon_theme = \"Tekketsu\" in the tekketsu profile's appearance"
        say "or always: gsettings set org.gnome.desktop.interface icon-theme Tekketsu"
    else
        say "use it with: gsettings set org.gnome.desktop.interface icon-theme Tela-tekketsu-black-dark"
        say "(paw's shell reads that at its next start)"
    fi
}

part_cursor() {
    title "Cursor"
    local tmp name=Bibata-Modern-Classic
    scratch
    fetch "https://github.com/ful1e5/Bibata_Cursor/releases/download/v2.0.7/$name.tar.xz" "$tmp/cursors.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$data/icons"
        tar -xJf "$tmp/cursors.tar.xz" -C "$data/icons" "$name"
        say "installed $name in $data/icons"
        say "use it with tekketsu: cursor_theme = \"$name\" in the tekketsu profile's appearance"
        say "or always: gsettings set org.gnome.desktop.interface cursor-theme $name"
    fi
}

[ $dry = 1 ] && echo "Dry run: nothing changes."
for part in "${parts[@]}"; do "part_$part"; done
[ $fonts = 1 ] && part_fonts
[ $icons = 1 ] && part_icons
[ $cursor = 1 ] && part_cursor
title "paw"
say "pick tekketsu from the panel's Appearance menu, or: paw msg profile tekketsu"
say "a configuration with profiles of its own adds it from rices/tekketsu/README.md"
