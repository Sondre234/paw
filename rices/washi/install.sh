#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Puts washi (和紙) on the applications around paw: kitty, fastfetch, Firefox, Discord, Claude
# Code and cdeck, Neovim and Helix, and with options the fonts, a caramel icon theme and a
# cursor. paw itself needs nothing installed: pick the `washi` or `sumi` profile from the panel's
# Appearance menu, or run `paw msg profile washi`.
#
#   install.sh [options] [parts...]
#
# Parts (all of them when none is named): kitty fastfetch firefox discord claude editors.
# Options:
#   --fonts         also download Space Grotesk, Maple Mono and the Nerd Font symbols (OFL/MIT)
#                   into ~/.local/share/fonts/paw-washi
#   --icons         also download the Tela icon theme (GPL-3.0) and install it recoloured in
#                   caramel as Tela-washi-brown (-light, -dark) in ~/.local/share/icons
#   --cursor        also download the Bibata Modern Classic cursor (GPL-3.0)
#   --no-reload     leave running kittys alone (they reload on their own once restarted)
#   --dark          make sumi, the night, kitty's theme rather than washi
#   --follow-system kitty switches between washi and sumi with the system's light or dark
#                   preference by itself (kitty 0.38+ auto themes; these override every colour
#                   kitty.conf sets while the system has a preference)
#   --dry-run       say what would change, change nothing
#   -h, --help      this text
#
# Every file it replaces is kept beside it as FILE.washi-backup (the first time only), and the
# lines it adds to your own files sit between `paw washi` markers, so running it again changes
# nothing new. It never edits paw's configuration, Discord itself, or settings it does not own.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
config=${XDG_CONFIG_HOME:-$HOME/.config}
data=${XDG_DATA_HOME:-$HOME/.local/share}
dry=0 fonts=0 icons=0 cursor=0 dark=0 reload=1 follow=0
parts=()

while [ $# -gt 0 ]; do
    case $1 in
        --fonts) fonts=1 ;;
        --icons) icons=1 ;;
        --cursor) cursor=1 ;;
        --dark) dark=1 ;;
        --no-reload) reload=0 ;;
        --follow-system) follow=1 ;;
        --dry-run) dry=1 ;;
        -h|--help) sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        kitty|fastfetch|firefox|discord|claude|editors) parts+=("$1") ;;
        *) echo "install.sh: unknown option or part: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
[ ${#parts[@]} -gt 0 ] || parts=(kitty fastfetch firefox discord claude editors)

say() { printf '  %s\n' "$*"; }
title() { printf '\033[1m%s\033[0m\n' "$*"; }

# Keeps a copy of FILE the first time washi replaces or edits it.
backup() {
    case $1 in */washi/*) return ;; esac # washi's own folders
    if [ -e "$1" ] && [ ! -e "$1.washi-backup" ]; then
        cp -a "$1" "$1.washi-backup"
        say "kept $1 as $1.washi-backup"
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

# Puts BLOCK (lines) into FILE between `paw washi` markers in comment style PREFIX, replacing
# the block an earlier run put there; the rest of the file stays as it is.
block() {
    local file=$1 prefix=$2 text=$3
    local begin="$prefix >>> paw washi (rices/washi/install.sh)" end="$prefix <<< paw washi"
    if [ -f "$file" ] && grep -qF "$begin" "$file"; then
        local current
        current=$(sed -n "\\|^$begin\$|,\\|^$end\$|p" "$file" | sed '1d;$d')
        if [ "$current" = "$text" ]; then
            say "up to date: $file"
            return
        fi
    fi
    if [ $dry = 1 ]; then
        say "would put the washi lines into $file"
        return
    fi
    mkdir -p "$(dirname "$file")"
    backup "$file"
    local rest=""
    if [ -f "$file" ]; then
        rest=$(sed "\\|^$begin\$|,\\|^$end\$|d" "$file")
    fi
    { [ -z "$rest" ] || printf '%s\n' "$rest"; printf '%s\n%s\n%s\n' "$begin" "$text" "$end"; } > "$file.washi-new"
    mv "$file.washi-new" "$file"
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

part_kitty() {
    title "kitty"
    local dir=$config/kitty
    place "$here/kitty/washi.conf" "$dir/washi/washi.conf"
    place "$here/kitty/sumi.conf" "$dir/washi/sumi.conf"
    # kitty 0.38 and newer switch between these with the system's light or dark preference.
    if [ $follow = 1 ]; then
        place "$here/kitty/washi.conf" "$dir/light-theme.auto.conf"
        place "$here/kitty/sumi.conf" "$dir/dark-theme.auto.conf"
    fi
    place "$here/kitty/$([ $dark = 1 ] && echo sumi || echo washi).conf" "$dir/washi/current.conf"
    block "$dir/kitty.conf" "#" "include washi/current.conf"
    if [ $dry = 0 ] && [ $reload = 1 ] && command -v pkill > /dev/null; then
        pkill -USR1 -x kitty 2> /dev/null && say "asked running kittys to reload" || true
    fi
}

part_fastfetch() {
    title "fastfetch"
    place "$here/fastfetch/washi.jsonc" "$config/fastfetch/washi.jsonc"
    place "$here/fastfetch/logo.png" "$config/fastfetch/washi/logo.png"
    say "run it with: fastfetch -c washi   (the seal shows in kitty)"
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
    # The start page finds the wallpapers two folders up, as it does in the rice itself.
    local start=$data/paw-washi/firefox/startpage
    for root in "$HOME/.mozilla/firefox" "$config/mozilla/firefox"; do
        profile=$(firefox_profile "$root") || continue
        [ -d "$profile" ] || continue
        found=1
        say "profile: $profile"
        for file in $(cd "$here/firefox/chrome" && find . -type f); do
            place "$here/firefox/chrome/$file" "$profile/chrome/${file#./}"
        done
        local prefs
        prefs=$(sed "s|file:///PATH/TO/startpage/index.html|file://$start/index.html|g" "$here/firefox/user.js")
        block "$profile/user.js" "//" "$prefs"
    done
    if [ $found = 0 ]; then
        say "no Firefox profile found (looked for profiles.ini in ~/.mozilla/firefox and ~/.config/mozilla/firefox)"
        return
    fi
    for file in $(cd "$here/firefox/startpage" && find . -type f); do
        place "$here/firefox/startpage/$file" "$start/${file#./}"
    done
    for picture in washi.jpg sumi.jpg; do
        place "$here/wallpaper/$picture" "$data/paw-washi/wallpaper/$picture"
    done
    say "restart Firefox to see it; the start page is $start/index.html"
}

part_discord() {
    title "Discord"
    local found=0 dir
    for dir in "$config/Vencord/themes" "$config/vesktop/themes" "$config/Equicord/themes" \
               "$config/equibop/themes" "$config/BetterDiscord/themes" \
               "$HOME/.var/app/dev.vencord.Vesktop/config/vesktop/themes"; do
        [ -d "$(dirname "$dir")" ] || continue
        found=1
        place "$here/discord/washi.theme.css" "$dir/washi.theme.css"
    done
    if [ $found = 0 ]; then
        say "no Vencord, Vesktop, Equicord, Equibop or BetterDiscord found: Discord itself cannot"
        say "load a theme. With one of them, put discord/washi.theme.css in its themes folder."
    else
        say "turn it on in the client mod's Themes settings; Discord's light theme is washi,"
        say "its dark ones sumi (Appearance → Sync with computer follows the system)"
    fi
}

part_claude() {
    title "Claude Code and cdeck"
    local dir=${CLAUDE_CONFIG_DIR:-$HOME/.claude}/themes
    place "$here/claude/washi.json" "$dir/washi.json"
    place "$here/claude/sumi.json" "$dir/sumi.json"
    say "pick it in Claude Code with /theme (custom:washi or custom:sumi); cdeck follows"
}

part_editors() {
    title "Neovim and Helix"
    place "$here/editors/nvim/colors/washi.lua" "$config/nvim/colors/washi.lua"
    place "$here/editors/nvim/colors/sumi.lua" "$config/nvim/colors/sumi.lua"
    place "$here/editors/helix/washi.toml" "$config/helix/themes/washi.toml"
    place "$here/editors/helix/sumi.toml" "$config/helix/themes/sumi.toml"
    say "Neovim: :colorscheme washi   Helix: theme = \"washi\""
}

part_fonts() {
    title "Fonts"
    local dir=$data/fonts/paw-washi tmp
    scratch
    local google=https://raw.githubusercontent.com/google/fonts/bd8f81ddb5c74d5c8897b36ad88b440266245103/ofl/spacegrotesk
    fetch "$google/SpaceGrotesk%5Bwght%5D.ttf" "$tmp/SpaceGrotesk[wght].ttf"
    fetch "$google/OFL.txt" "$tmp/SpaceGrotesk-OFL.txt"
    fetch https://github.com/subframe7536/maple-font/releases/download/v7.9/MapleMono-TTF.zip "$tmp/maple.zip"
    fetch https://github.com/ryanoasis/nerd-fonts/releases/download/v3.5.1/NerdFontsSymbolsOnly.tar.xz "$tmp/symbols.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$dir/MapleMono" "$dir/NerdFontsSymbols"
        cp "$tmp/SpaceGrotesk[wght].ttf" "$tmp/SpaceGrotesk-OFL.txt" "$dir/"
        python3 -I -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$tmp/maple.zip" "$dir/MapleMono"
        tar -xJf "$tmp/symbols.tar.xz" -C "$dir/NerdFontsSymbols"
        fc-cache -f "$dir" > /dev/null 2>&1 || true
        say "installed Space Grotesk, Maple Mono and the Nerd Font symbols in $dir"
    fi
}

part_icons() {
    title "Icons"
    local tmp version=2026-07-07
    scratch
    fetch "https://github.com/vinceliuice/Tela-icon-theme/archive/refs/tags/$version.tar.gz" "$tmp/tela.tar.gz"
    if [ $dry = 0 ]; then
        tar -xzf "$tmp/tela.tar.gz" -C "$tmp"
        # Tela's brown folders, in the caramel of paw's toe beans instead.
        sed -i "s/local -r theme_color='#795548'/local -r theme_color='#a87650'/" \
            "$tmp/Tela-icon-theme-$version/install.sh"
        (cd "$tmp/Tela-icon-theme-$version" && bash ./install.sh -d "$data/icons" -n Tela-washi brown > /dev/null)
        say "installed Tela-washi-brown (and -light, -dark) in $data/icons"
        say "use it with: gsettings set org.gnome.desktop.interface icon-theme Tela-washi-brown"
        say "(paw's shell reads it at its next start)"
    fi
}

part_cursor() {
    title "Cursor"
    local tmp
    scratch
    fetch https://github.com/ful1e5/Bibata_Cursor/releases/download/v2.0.7/Bibata-Modern-Classic.tar.xz "$tmp/bibata.tar.xz"
    if [ $dry = 0 ]; then
        mkdir -p "$data/icons"
        tar -xJf "$tmp/bibata.tar.xz" -C "$data/icons"
        say "installed Bibata-Modern-Classic in $data/icons"
        say "use it with: gsettings set org.gnome.desktop.interface cursor-theme Bibata-Modern-Classic"
    fi
}

[ $dry = 1 ] && echo "Dry run: nothing changes."
for part in "${parts[@]}"; do "part_$part"; done
[ $fonts = 1 ] && part_fonts
[ $icons = 1 ] && part_icons
[ $cursor = 1 ] && part_cursor
title "paw"
say "pick washi (day) or sumi (night) from the panel's Appearance menu, or: paw msg profile washi"
say "a configuration with profiles of its own adds them from rices/washi/README.md"
