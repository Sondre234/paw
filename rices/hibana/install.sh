#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Puts hibana (火花) on the applications around paw: kitty, fastfetch, Firefox, Discord, Claude
# Code and cdeck, Neovim and Helix, and GTK 4 / libadwaita; with options the fonts, an ash and
# ember icon theme with hibana's own icons over it, and a black cursor.
# paw itself needs nothing installed: pick the `hibana` profile from the panel's Appearance
# menu, or run `paw msg profile hibana`.
#
#   install.sh [options] [parts...]
#
# Parts (all of them when none is named): kitty fastfetch firefox discord claude editors gtk
# wallpapers (hibana's seven, where paw's wallpaper picker looks: Pictures/wallpapers/hibana,
# or Pictures/hibana when there is no wallpapers folder).
# Options:
#   --fonts      also download Barlow (and Barlow Condensed), Dela Gothic One, JetBrains Mono
#                and the Nerd Font symbols (OFL/MIT) into ~/.local/share/fonts/paw-hibana
#   --icons      also download the Tela icon theme (GPL-3.0) and install it recoloured, ash
#                folders with ember symbols, as Tela-hibana-black (-light, -dark), and put
#                hibana's own icons over it as the Hibana theme, in ~/.local/share/icons
#   --cursor     also download the Bibata Modern Classic cursors (GPL-3.0), black with a white
#                edge
#   --no-reload  leave running kittys alone (they reload on their own once restarted)
#   --dry-run    say what would change, change nothing
#   -h, --help   this text
#
# Every file it replaces is kept beside it as FILE.hibana-backup (the first time only), and the
# lines it adds to your own files sit between `paw hibana` markers, so running it again changes
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

# Keeps a copy of FILE the first time hibana replaces or edits it.
backup() {
    case $1 in */hibana/*|*/paw-hibana/*) return ;; esac # hibana's own folders
    if [ -e "$1" ] && [ ! -e "$1.hibana-backup" ]; then
        cp -a "$1" "$1.hibana-backup"
        say "kept $1 as $1.hibana-backup"
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

# Copies every file under the directory SOURCE into the directory TARGET, as place does.
place_tree() {
    local source=$1 target=$2 file
    while IFS= read -r -d '' file; do
        place "$source/$file" "$target/$file"
    done < <(cd "$source" && find . -type f -printf '%P\0' | sort -z)
}

# Puts BLOCK (lines) into FILE between `paw hibana` markers in comment style PREFIX (and SUFFIX,
# for comments that close), replacing the block an earlier run put there; the rest of the file
# stays as it is.
block() {
    local file=$1 prefix=$2 text=$3 suffix=${4:-}
    local begin="$prefix >>> paw hibana (rices/hibana/install.sh)$suffix" end="$prefix <<< paw hibana$suffix"
    if [ -f "$file" ] && grep -qxF "$begin" "$file"; then
        local current
        current=$(awk -v b="$begin" -v e="$end" '$0 == b {on = 1; next} $0 == e {on = 0} on' "$file")
        if [ "$current" = "$text" ]; then
            say "up to date: $file"
            return
        fi
    fi
    if [ $dry = 1 ]; then
        say "would put the hibana lines into $file"
        return
    fi
    mkdir -p "$(dirname "$file")"
    backup "$file"
    local rest=""
    if [ -f "$file" ]; then
        rest=$(awk -v b="$begin" -v e="$end" '$0 == b {skip = 1} !skip {print} $0 == e {skip = 0}' "$file")
    fi
    { [ -z "$rest" ] || printf '%s\n' "$rest"; printf '%s\n%s\n%s\n' "$begin" "$text" "$end"; } > "$file.hibana-new"
    mv "$file.hibana-new" "$file"
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
    place "$here/kitty/hibana.conf" "$dir/hibana/hibana.conf"
    block "$dir/kitty.conf" "#" "include hibana/hibana.conf"
    if [ $dry = 0 ] && [ $reload = 1 ] && command -v pkill > /dev/null; then
        pkill -USR1 -x kitty 2> /dev/null && say "asked running kittys to reload" || true
    fi
    say "the glass needs kitty 0.37 or newer (transparent_background_colors)"
}

part_fastfetch() {
    title "fastfetch"
    place "$here/fastfetch/hibana.jsonc" "$config/fastfetch/hibana.jsonc"
    if [ -f "$here/fastfetch/logo.png" ]; then
        place "$here/fastfetch/logo.png" "$config/fastfetch/hibana/logo.png"
    else
        say "no fastfetch/logo.png here: the spec sheet shows without the cat"
    fi
    say "run it with: fastfetch -c hibana   (the cat shows in kitty)"
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
    local start=$data/paw-hibana/firefox/startpage
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
        place_tree "$here/wallpaper" "$data/paw-hibana/wallpaper"
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
        place "$here/discord/hibana.theme.css" "$dir/hibana.theme.css"
    done
    if [ $found = 0 ]; then
        say "no Vencord, Vesktop, Equicord, Equibop or BetterDiscord found: Discord itself cannot"
        say "load a theme. With one of them, put discord/hibana.theme.css in its themes folder."
    else
        say "turn it on in the client mod's Themes settings, with Discord on a dark theme; for glass,"
        say "turn on window transparency and put  :root { --hibana-glass: 1; }  in QuickCSS"
    fi
}

part_claude() {
    title "Claude Code and cdeck"
    local dir=${CLAUDE_CONFIG_DIR:-$HOME/.claude}/themes
    place "$here/claude/hibana.json" "$dir/hibana.json"
    say "pick it in Claude Code with /theme (custom:hibana); cdeck follows"
}

part_editors() {
    title "Neovim and Helix"
    place "$here/editors/nvim/colors/hibana.lua" "$config/nvim/colors/hibana.lua"
    place "$here/editors/helix/hibana.toml" "$config/helix/themes/hibana.toml"
    say "Neovim: :colorscheme hibana   Helix: theme = \"hibana\""
}

part_gtk() {
    title "GTK 4 and libadwaita"
    block "$config/gtk-4.0/gtk.css" "/*" "$(cat "$here/gtk/gtk.css")" " */"
    say "libadwaita applications take it when they next start, while the system prefers dark"
}

# The wallpapers beside the others in the folder paw's wallpaper picker shows when
# shell.wallpapers is unset (Pictures/wallpapers, else Pictures itself, searched with its
# subfolders), in a hibana folder of their own; creating a wallpapers folder would move the
# picker away from the pictures it shows now.
part_wallpapers() {
    title "Wallpapers"
    local pictures
    pictures=$(xdg-user-dir PICTURES 2>/dev/null || true)
    [ -n "$pictures" ] && [ "$pictures" != "$HOME" ] || pictures=$HOME/Pictures
    [ -d "$pictures/wallpapers" ] && pictures=$pictures/wallpapers
    place_tree "$here/wallpaper" "$pictures/hibana"
    say "the wallpaper picker lists them under hibana (unless shell.wallpapers names another folder)"
}

part_fonts() {
    title "Fonts"
    local dir=$data/fonts/paw-hibana tmp weight
    scratch
    # Barlow as single weights (it ships no variable font), Barlow Condensed's thin ones for the
    # start page's clock, Dela Gothic One for Japanese headings in a bold manga hand, JetBrains
    # Mono for the terminal.
    local fonts=https://raw.githubusercontent.com/google/fonts/690b65d64dabaebf9d18e2d7e8c8e7e02b4cccaf/ofl
    for weight in Light Regular Italic Medium SemiBold Bold BoldItalic; do
        fetch "$fonts/barlow/Barlow-$weight.ttf" "$tmp/Barlow-$weight.ttf"
    done
    fetch "$fonts/barlow/OFL.txt" "$tmp/Barlow-OFL.txt"
    for weight in ExtraLight Light; do
        fetch "$fonts/barlowcondensed/BarlowCondensed-$weight.ttf" "$tmp/BarlowCondensed-$weight.ttf"
    done
    fetch "$fonts/delagothicone/DelaGothicOne-Regular.ttf" "$tmp/DelaGothicOne-Regular.ttf"
    fetch "$fonts/delagothicone/OFL.txt" "$tmp/DelaGothicOne-OFL.txt"
    fetch https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip "$tmp/jetbrains.zip"
    fetch https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/NerdFontsSymbolsOnly.tar.xz "$tmp/symbols.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$dir/Barlow" "$dir/DelaGothicOne" "$dir/JetBrainsMono" "$dir/NerdFontsSymbols"
        cp "$tmp"/Barlow-* "$tmp"/BarlowCondensed-* "$dir/Barlow/"
        cp "$tmp"/DelaGothicOne-* "$dir/DelaGothicOne/"
        mkdir -p "$tmp/jetbrains"
        unzip_into "$tmp/jetbrains.zip" "$tmp/jetbrains"
        cp "$tmp"/jetbrains/fonts/ttf/JetBrainsMono-*.ttf "$tmp/jetbrains/OFL.txt" "$dir/JetBrainsMono/"
        tar -xJf "$tmp/symbols.tar.xz" -C "$dir/NerdFontsSymbols"
        fc-cache -f "$dir" > /dev/null 2>&1 || true
        say "installed Barlow, Dela Gothic One, JetBrains Mono and the Nerd Font symbols in $dir"
    fi
}

part_icons() {
    title "Icons"
    local tmp version=2026-07-07
    scratch
    fetch "https://github.com/vinceliuice/Tela-icon-theme/archive/refs/tags/$version.tar.gz" "$tmp/tela.tar.gz"
    if [ $dry = 0 ]; then
        tar -xzf "$tmp/tela.tar.gz" -C "$tmp"
        # Tela's black folders in hibana's ash, their symbols in ember.
        sed -i "/^ *black)/,/;;/{s/theme_color='#4d4d4d'/theme_color='#36323a'/;s/theme_back_color='#ffffff'/theme_back_color='#ff7a33'/}" \
            "$tmp/Tela-icon-theme-$version/install.sh"
        (cd "$tmp/Tela-icon-theme-$version" && bash ./install.sh -d "$data/icons" -n Tela-hibana black) > /dev/null 2>&1 \
            || { echo "install.sh: Tela's own installer failed" >&2; exit 1; }
        say "installed Tela-hibana-black (and -light, -dark) in $data/icons"
    fi
    # hibana's own icons over it (they inherit Tela-hibana-black-dark for everything else).
    if [ -d "$here/icons/Hibana" ]; then
        place_tree "$here/icons/Hibana" "$data/icons/Hibana"
        if [ $dry = 0 ]; then
            gtk-update-icon-cache -q -t -f "$data/icons/Hibana" 2> /dev/null || true
        fi
        say "use it with hibana: icon_theme = \"Hibana\" in the hibana profile's appearance"
        say "or always: gsettings set org.gnome.desktop.interface icon-theme Hibana"
    else
        say "use it with: gsettings set org.gnome.desktop.interface icon-theme Tela-hibana-black-dark"
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
        say "use it with hibana: cursor_theme = \"$name\" in the hibana profile's appearance"
        say "or always: gsettings set org.gnome.desktop.interface cursor-theme $name"
    fi
}

[ $dry = 1 ] && echo "Dry run: nothing changes."
for part in "${parts[@]}"; do "part_$part"; done
[ $fonts = 1 ] && part_fonts
[ $icons = 1 ] && part_icons
[ $cursor = 1 ] && part_cursor
title "paw"
say "pick hibana from the panel's Appearance menu, or: paw msg profile hibana"
say "a configuration with profiles of its own adds it from rices/hibana/README.md"
