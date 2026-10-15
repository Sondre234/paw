#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Puts mochi (餅) on the applications around paw: kitty, fastfetch, Firefox, Discord, Claude
# Code and cdeck, Neovim and Helix, and GTK 4 / libadwaita; with options the fonts, a mauve icon
# theme with mochi's own icons over it, and a cursor. Colours from Catppuccin Mocha (MIT).
# paw itself needs nothing installed: pick the `mochi` profile from the panel's Appearance
# menu, or run `paw msg profile mochi`.
#
#   install.sh [options] [parts...]
#
# Parts (all of them when none is named): kitty fastfetch firefox discord claude editors gtk
# wallpapers (mochi's seven, where paw's wallpaper picker looks: Pictures/wallpapers/mochi, or
# Pictures/mochi when there is no wallpapers folder).
# Options:
#   --fonts      also download Nunito, M PLUS Rounded 1c, Maple Mono and the Nerd Font symbols
#                (OFL/MIT) into ~/.local/share/fonts/paw-mochi
#   --icons      also download the Tela icon theme (GPL-3.0) and install it recoloured in
#                mauve as Tela-mochi-purple (-light, -dark), and put mochi's own icons over
#                it as the Mochi theme, in ~/.local/share/icons
#   --cursor     also download Catppuccin's Mocha Light cursors (GPL-2.0)
#   --no-reload  leave running kittys alone (they reload on their own once restarted)
#   --dry-run    say what would change, change nothing
#   -h, --help   this text
#
# Every file it replaces is kept beside it as FILE.mochi-backup (the first time only), and the
# lines it adds to your own files sit between `paw mochi` markers, so running it again changes
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
        -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        kitty|fastfetch|firefox|discord|claude|editors|gtk|wallpapers) parts+=("$1") ;;
        *) echo "install.sh: unknown option or part: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
[ ${#parts[@]} -gt 0 ] || parts=(kitty fastfetch firefox discord claude editors gtk wallpapers)

say() { printf '  %s\n' "$*"; }
title() { printf '\033[1m%s\033[0m\n' "$*"; }

# Keeps a copy of FILE the first time mochi replaces or edits it.
backup() {
    case $1 in */mochi/*|*/paw-mochi/*) return ;; esac # mochi's own folders
    if [ -e "$1" ] && [ ! -e "$1.mochi-backup" ]; then
        cp -a "$1" "$1.mochi-backup"
        say "kept $1 as $1.mochi-backup"
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

# Puts BLOCK (lines) into FILE between `paw mochi` markers in comment style PREFIX (and SUFFIX,
# for comments that close), replacing the block an earlier run put there; the rest of the file
# stays as it is.
block() {
    local file=$1 prefix=$2 text=$3 suffix=${4:-}
    local begin="$prefix >>> paw mochi (rices/mochi/install.sh)$suffix" end="$prefix <<< paw mochi$suffix"
    if [ -f "$file" ] && grep -qxF "$begin" "$file"; then
        local current
        current=$(awk -v b="$begin" -v e="$end" '$0 == b {on = 1; next} $0 == e {on = 0} on' "$file")
        if [ "$current" = "$text" ]; then
            say "up to date: $file"
            return
        fi
    fi
    if [ $dry = 1 ]; then
        say "would put the mochi lines into $file"
        return
    fi
    mkdir -p "$(dirname "$file")"
    backup "$file"
    local rest=""
    if [ -f "$file" ]; then
        rest=$(awk -v b="$begin" -v e="$end" '$0 == b {skip = 1} !skip {print} $0 == e {skip = 0}' "$file")
    fi
    { [ -z "$rest" ] || printf '%s\n' "$rest"; printf '%s\n%s\n%s\n' "$begin" "$text" "$end"; } > "$file.mochi-new"
    mv "$file.mochi-new" "$file"
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
    place "$here/kitty/mochi.conf" "$dir/mochi/mochi.conf"
    block "$dir/kitty.conf" "#" "include mochi/mochi.conf"
    if [ $dry = 0 ] && [ $reload = 1 ] && command -v pkill > /dev/null; then
        pkill -USR1 -x kitty 2> /dev/null && say "asked running kittys to reload" || true
    fi
    say "the glass needs kitty 0.37 or newer (transparent_background_colors)"
}

part_fastfetch() {
    title "fastfetch"
    place "$here/fastfetch/mochi.jsonc" "$config/fastfetch/mochi.jsonc"
    if [ -f "$here/fastfetch/logo.png" ]; then
        place "$here/fastfetch/logo.png" "$config/fastfetch/mochi/logo.png"
    else
        say "no fastfetch/logo.png here: the spec sheet shows without the cat"
    fi
    say "run it with: fastfetch -c mochi   (the cat shows in kitty)"
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
    local start=$data/paw-mochi/firefox/startpage
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
        place_tree "$here/wallpaper" "$data/paw-mochi/wallpaper"
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
        place "$here/discord/mochi.theme.css" "$dir/mochi.theme.css"
    done
    if [ $found = 0 ]; then
        say "no Vencord, Vesktop, Equicord, Equibop or BetterDiscord found: Discord itself cannot"
        say "load a theme. With one of them, put discord/mochi.theme.css in its themes folder."
    else
        say "turn it on in the client mod's Themes settings, with Discord on a dark theme; for glass,"
        say "turn on window transparency and put  :root { --mochi-glass: 1; }  in QuickCSS"
    fi
}

part_claude() {
    title "Claude Code and cdeck"
    local dir=${CLAUDE_CONFIG_DIR:-$HOME/.claude}/themes
    place "$here/claude/mochi.json" "$dir/mochi.json"
    say "pick it in Claude Code with /theme (custom:mochi); cdeck follows"
}

part_editors() {
    title "Neovim and Helix"
    place "$here/editors/nvim/colors/mochi.lua" "$config/nvim/colors/mochi.lua"
    place "$here/editors/helix/mochi.toml" "$config/helix/themes/mochi.toml"
    say "Neovim: :colorscheme mochi   Helix: theme = \"mochi\""
}

part_gtk() {
    title "GTK 4 and libadwaita"
    block "$config/gtk-4.0/gtk.css" "/*" "$(cat "$here/gtk/gtk.css")" " */"
    say "libadwaita applications take it when they next start, while the system prefers dark"
}

# The wallpapers beside the others in the folder paw's wallpaper picker shows when
# shell.wallpapers is unset (Pictures/wallpapers, else Pictures itself, searched with its
# subfolders), in a mochi folder of their own; creating a wallpapers folder would move the
# picker away from the pictures it shows now.
part_wallpapers() {
    title "Wallpapers"
    local pictures
    pictures=$(xdg-user-dir PICTURES 2>/dev/null || true)
    [ -n "$pictures" ] && [ "$pictures" != "$HOME" ] || pictures=$HOME/Pictures
    [ -d "$pictures/wallpapers" ] && pictures=$pictures/wallpapers
    place_tree "$here/wallpaper" "$pictures/mochi"
    say "the wallpaper picker lists them under mochi (unless shell.wallpapers names another folder)"
}

part_fonts() {
    title "Fonts"
    local dir=$data/fonts/paw-mochi tmp weight
    scratch
    # Nunito as single weights (the variable font draws one weight in Firefox and Qt), and
    # M PLUS Rounded 1c for Japanese in the same rounded hand.
    local nunito=https://raw.githubusercontent.com/google/fonts/3bdd17c90e2160a99205a43cb56d60fba5847eca/ofl/nunito
    local mplus=https://raw.githubusercontent.com/google/fonts/bd8f81ddb5c74d5c8897b36ad88b440266245103/ofl/mplusrounded1c
    for weight in Light Regular Italic SemiBold Bold BoldItalic ExtraBold Black; do
        fetch "$nunito/Nunito-$weight.ttf" "$tmp/Nunito-$weight.ttf"
    done
    fetch "$nunito/OFL.txt" "$tmp/Nunito-OFL.txt"
    fetch "$mplus/MPLUSRounded1c-Regular.ttf" "$tmp/MPLUSRounded1c-Regular.ttf"
    fetch "$mplus/MPLUSRounded1c-Bold.ttf" "$tmp/MPLUSRounded1c-Bold.ttf"
    fetch https://github.com/subframe7536/maple-font/releases/download/v7.9/MapleMono-TTF.zip "$tmp/maple.zip"
    fetch https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/NerdFontsSymbolsOnly.tar.xz "$tmp/symbols.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$dir/Nunito" "$dir/MPLUSRounded1c" "$dir/MapleMono" "$dir/NerdFontsSymbols"
        cp "$tmp"/Nunito-* "$dir/Nunito/"
        cp "$tmp"/MPLUSRounded1c-* "$dir/MPLUSRounded1c/"
        unzip_into "$tmp/maple.zip" "$dir/MapleMono"
        tar -xJf "$tmp/symbols.tar.xz" -C "$dir/NerdFontsSymbols"
        fc-cache -f "$dir" > /dev/null 2>&1 || true
        say "installed Nunito, M PLUS Rounded 1c, Maple Mono and the Nerd Font symbols in $dir"
    fi
}

part_icons() {
    title "Icons"
    local tmp version=2026-07-07
    scratch
    fetch "https://github.com/vinceliuice/Tela-icon-theme/archive/refs/tags/$version.tar.gz" "$tmp/tela.tar.gz"
    if [ $dry = 0 ]; then
        tar -xzf "$tmp/tela.tar.gz" -C "$tmp"
        # Tela's purple folders in mochi's mauve, their symbols in Mocha's base.
        sed -i "/^ *purple)/,/;;/{s/theme_color='#7e57c2'/theme_color='#cba6f7'/;s/theme_back_color='#ffffff'/theme_back_color='#1e1e2e'/}" \
            "$tmp/Tela-icon-theme-$version/install.sh"
        (cd "$tmp/Tela-icon-theme-$version" && bash ./install.sh -d "$data/icons" -n Tela-mochi purple) > /dev/null 2>&1 \
            || { echo "install.sh: Tela's own installer failed" >&2; exit 1; }
        say "installed Tela-mochi-purple (and -light, -dark) in $data/icons"
    fi
    # mochi's own icons over it (they inherit Tela-mochi-purple-dark for everything else).
    if [ -d "$here/icons/Mochi" ]; then
        place_tree "$here/icons/Mochi" "$data/icons/Mochi"
        if [ $dry = 0 ]; then
            gtk-update-icon-cache -q -t -f "$data/icons/Mochi" 2> /dev/null || true
        fi
        say "use it with: gsettings set org.gnome.desktop.interface icon-theme Mochi"
    else
        say "use it with: gsettings set org.gnome.desktop.interface icon-theme Tela-mochi-purple-dark"
    fi
    say "(paw's shell reads it at its next start)"
}

part_cursor() {
    title "Cursor"
    local tmp name=catppuccin-mocha-light-cursors
    scratch
    fetch "https://github.com/catppuccin/cursors/releases/download/v2.0.0/$name.zip" "$tmp/cursors.zip"
    if [ $dry = 0 ]; then
        mkdir -p "$data/icons"
        unzip_into "$tmp/cursors.zip" "$data/icons"
        say "installed $name in $data/icons"
        say "use it with: gsettings set org.gnome.desktop.interface cursor-theme $name"
    fi
}

[ $dry = 1 ] && echo "Dry run: nothing changes."
for part in "${parts[@]}"; do "part_$part"; done
[ $fonts = 1 ] && part_fonts
[ $icons = 1 ] && part_icons
[ $cursor = 1 ] && part_cursor
title "paw"
say "pick mochi from the panel's Appearance menu, or: paw msg profile mochi"
say "a configuration with profiles of its own adds it from rices/mochi/README.md"
