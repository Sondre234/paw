# Copyright 2026 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

LUA_COMPAT=( lua5-4 )
PYTHON_COMPAT=( python3_{12..14} )
inherit cmake lua-single optfeature python-any-r1

DESCRIPTION="Mouse-first Wayland desktop with a Qt Quick shell and Lua configuration"
HOMEPAGE="https://github.com/Sondre234/paw"

if [[ ${PV} == 9999 ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/Sondre234/paw.git"
else
	SRC_URI="https://github.com/Sondre234/paw/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="~amd64"
fi

# GPL-3+ for paw; MIT for the TinyWL-derived adapter and the wlr protocol files.
LICENSE="GPL-3+ MIT"
SLOT="0"
IUSE="X +notifications +pulseaudio +shell test"
REQUIRED_USE="
	${LUA_REQUIRED_USE}
	notifications? ( shell )
	pulseaudio? ( shell )
"
RESTRICT="!test? ( test )"

# sd-bus (from whichever of systemd, elogind or basu is installed) takes a logind sleep
# inhibitor while a standalone session is on screen.
COMMON_DEPEND="
	${LUA_DEPS}
	dev-libs/libinput:=
	dev-libs/wayland
	gui-libs/wlroots:0.20[drm,libinput,session,X=]
	x11-libs/libxkbcommon
	|| (
		sys-apps/systemd
		sys-auth/elogind
		sys-libs/basu
	)
	X? ( x11-libs/libxcb:= )
	shell? (
		dev-libs/glib:2
		>=dev-qt/qtbase-6.5:6[gui,network,wayland]
		>=dev-qt/qtdeclarative-6.5:6
		>=kde-plasma/layer-shell-qt-6.6:6
		notifications? ( dev-qt/qtbase:6[dbus] )
		pulseaudio? ( media-libs/libpulse )
	)
"
RDEPEND="
	${COMMON_DEPEND}
	x11-misc/xkeyboard-config
	X? ( x11-base/xwayland )
	shell? ( dev-qt/qtsvg:6 )
"
DEPEND="${COMMON_DEPEND}"
BDEPEND="
	dev-libs/wayland-protocols
	dev-util/wayland-scanner
	virtual/pkgconfig
	test? (
		${PYTHON_DEPS}
		sys-apps/dbus
	)
"

pkg_setup() {
	lua-single_pkg_setup
	use test && python-any-r1_pkg_setup
}

src_configure() {
	local mycmakeargs=(
		-DPAW_BUILD_COMPOSITOR=ON
		-DPAW_BUILD_SHELL=$(usex shell)
		-DPAW_INSTALL_SESSION=ON
		-DBUILD_TESTING=$(usex test)
	)
	use shell && mycmakeargs+=(
		-DPAW_NOTIFICATIONS=$(usex notifications)
		-DPAW_PULSEAUDIO=$(usex pulseaudio)
	)
	cmake_src_configure
}

src_test() {
	# The suite runs headless compositors (pixman) and the shell offscreen; Xwayland, started
	# by the X11 tests, looks for a GPU and does without one.
	addpredict /dev/dri
	local -x QT_QPA_PLATFORM=offscreen
	cmake_src_test
}

src_install() {
	cmake_src_install
	# Portage records the licenses; the protocol files keep their notices.
	rm -r "${ED}"/usr/share/licenses || die
}

pkg_postinst() {
	optfeature_header "Programs the default configuration and the desktop use when present:"
	optfeature "a terminal for Super+Q (the first one installed)" x11-terms/kitty gui-apps/foot \
		x11-terms/alacritty x11-terms/wezterm kde-apps/konsole x11-terms/gnome-terminal \
		x11-terms/xterm
	optfeature "screenshots (Print)" "gui-apps/grim gui-apps/slurp"
	optfeature "copying screenshots to the clipboard" gui-apps/wl-clipboard
	optfeature "screen sharing" "gui-libs/xdg-desktop-portal-wlr media-video/pipewire"
	optfeature "file choosers and other portals" sys-apps/xdg-desktop-portal-gtk
	optfeature "screen locking" gui-apps/swaylock
	optfeature "idle timeouts" gui-apps/swayidle
	optfeature "application icons in the shell" x11-themes/adwaita-icon-theme \
		kde-frameworks/breeze-icons
}
