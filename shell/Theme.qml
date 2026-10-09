// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The shell's design tokens: colours, type, radii, spacing, icon sizes and motion, derived from
// the appearance profile in use (shell.panel_color, text_color, accent, font, font_size and
// windows.urgent_color) and the animation settings, so that every part of the shell draws from
// one set and follows a profile switch at once.
//
// States (hover, pressed, selected) and lines are the text colour laid over what is under them
// at a low opacity. That shows on a light panel as on a dark one, where lightening the panel
// colour does nothing to a near-white one.
QtObject {
    // The macOS style (shell.style): a menu bar along the top and a dock at the bottom instead
    // of the taskbar, and popups drawn as macOS draws them.
    readonly property bool macos: shell.style === "macos"

    // Surfaces. The bar keeps the configured colour, translucency included. Popups are opaque:
    // nothing is blurred behind them, and a window showing through makes text hard to read.
    readonly property color bar: shell.panelColor
    readonly property color surface: Qt.rgba(bar.r, bar.g, bar.b, 1)
    // A card, field or other block inside a popup.
    readonly property color surfaceRaised: mix(surface, text, 0.04)
    readonly property color surfaceRaisedHover: mix(surface, text, 0.09)
    // Whether the surfaces are light, for the few colours that cannot be derived from them.
    readonly property bool light: luminance(surface) > 0.5

    // States, laid over a surface or the bar.
    readonly property color hover: alpha(text, 0.08)
    readonly property color pressed: alpha(text, 0.18)
    // What is open, current or on: a button whose popup is open, the current workspace.
    readonly property color selected: alpha(text, 0.14)

    // What dims everything behind a dialog.
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.45)

    // Lines: a popup's outline, a separator.
    readonly property color border: alpha(text, 0.14)
    readonly property color divider: alpha(text, 0.12)

    // Text and line icons.
    readonly property color text: shell.textColor
    readonly property color textMuted: alpha(text, 0.62)
    readonly property color textDisabled: alpha(text, 0.4)

    readonly property color accent: shell.accent
    // Text and icons on an accent fill: whichever of the surface and text colours stands out
    // more against it.
    readonly property color textOnAccent: contrast(accent, surface) >= contrast(accent, text)
                                          ? surface : text
    readonly property color accentHover: mix(accent, textOnAccent, 0.15)
    // A selected row or tab that is not filled with the accent.
    readonly property color accentSubtle: alpha(accent, 0.22)

    // Something going wrong: text and outlines on a surface, a fill with textOnDanger on it, and
    // a surface for a message.
    readonly property color danger: light ? "#c42b1c" : "#ff6b6b"
    readonly property color dangerFill: "#c42b1c"
    readonly property color textOnDanger: "#ffffff"
    readonly property color dangerSurface: mix(surface, dangerFill, light ? 0.18 : 0.35)

    // A window asking for attention (windows.urgent_color).
    readonly property color urgent: shell.urgentColor
    readonly property color urgentSubtle: alpha(urgent, 0.24)

    // Type, from shell.font and shell.font_size: body text, then smaller for secondary text and
    // captions, larger for list entries and popup headings, a title for a dialog or an overlay's
    // input, and the launcher's heading.
    readonly property string fontFamily: shell.fontFamily.length > 0 ? shell.fontFamily
                                                                   : Qt.application.font.family
    readonly property int fontSize: shell.fontSize
    readonly property int fontSizeSmall: Math.max(6, fontSize - 1)
    readonly property int fontSizeCaption: Math.max(6, fontSize - 2)
    readonly property int fontSizeLarge: fontSize + 2
    readonly property int fontSizeTitle: fontSize + 4
    readonly property int fontSizeDisplay: Math.round(fontSize * 1.75)

    // Corners: buttons and rows, menus and small popups, large popups.
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14

    readonly property int spacingXS: 2
    readonly property int spacingS: 4
    readonly property int spacingM: 8
    readonly property int spacingL: 12
    readonly property int spacingXL: 16
    readonly property int spacingXXL: 20

    // A row of a menu or a list, and a section's heading over rows.
    readonly property int rowHeight: 36
    readonly property int headingHeight: 30
    // A button on the bar: as tall as the bar (shell.panel_height) leaves with a small margin
    // above and below, and an icon's as wide as Windows 11's. In the macOS style the widgets are
    // on the menu bar, and these are its buttons.
    readonly property real barButtonHeight: macos ? menuBarHeight - 2 * menuBarInset
                                                  : Math.max(0, shell.panelHeight - 2 * spacingS)
    readonly property int barButtonWidth: macos ? menuBarIconSize + 2 * menuBarPadding - spacingS : 40

    // The macOS style's menu bar and dock.
    // The menu bar: as tall as macOS's for the font size (28 pixels for 13), its items padded on
    // either side, and the highlight under an open one rounded and inset from the bar's edges.
    readonly property int menuBarHeight: Math.round(fontSize * 2.15)
    readonly property int menuBarPadding: 10
    readonly property int menuBarInset: 3
    readonly property int menuBarRadius: 5
    readonly property int menuBarIconSize: 16
    // The dock: applications' icons as large as shell.panel_height leaves inside its padding,
    // this far apart, with a dot under a running one; the line before the Trash; and its outline,
    // a light edge inside and a faint dark one outside.
    readonly property int dockPadding: 8
    readonly property real dockIconSize: Math.max(16, shell.panelHeight - 2 * dockPadding)
    readonly property int dockSpacing: 6
    readonly property int dockDotSize: 4
    readonly property color dockSeparator: alpha(text, 0.22)
    readonly property color dockInnerEdge: Qt.rgba(1, 1, 1, light ? 0.55 : 0.14)
    readonly property color dockOuterEdge: Qt.rgba(0, 0, 0, light ? 0.12 : 0.5)
    // An icon bouncing as its application starts (until a window opens) or asks for attention:
    // up a share of its size and back, a bounce this long, at most this many times.
    readonly property real dockBounceHeight: 0.35
    readonly property int dockBounceDuration: duration(600)
    readonly property int dockBounces: 3

    // Line icons in menus, line icons on the bar, applications' icons on the bar and in lists,
    // and in the launcher.
    readonly property int iconSizeSmall: 16
    readonly property int iconSize: 18
    readonly property int appIconSize: 22
    readonly property int appIconSizeLarge: 30
    // A line icon saying what an overlay shows, as the on-screen display's.
    readonly property int iconSizeLarge: 22
    // An application's icon standing for a window in an overlay, as in the switcher's grid.
    readonly property int appIconSizeDisplay: 64

    // Motion, following animations.enabled and animations.speed: every duration is 0 while
    // animations are off.
    readonly property bool animations: shell.animations
    readonly property real motionScale: animations ? 1 / Math.max(0.1, shell.animationSpeed) : 0
    readonly property int durationFast: duration(120)
    readonly property int durationNormal: duration(180)
    readonly property int durationSlow: duration(300)
    // Something arriving or moving, and something leaving.
    readonly property int easing: Easing.OutCubic
    readonly property int easingExit: Easing.InCubic
    // How small a small thing (a taskbar button, a badge) starts as it grows in, and ends as it
    // shrinks away.
    readonly property real growFrom: 0.5
    // How small an icon on the bar gets while its button is pressed.
    readonly property real pressScale: 0.88

    // Whether shader effects (shadows) can be drawn: only through the GPU.
    readonly property bool effects: shell.effects
    // The shadow under a popup (PopupCard), drawn only with effects: how dark, how soft and how
    // far down. A light profile needs less of it to show.
    readonly property color shadow: Qt.rgba(0, 0, 0, light ? 0.3 : 0.6)
    readonly property int shadowBlur: 20
    readonly property int shadowOffset: 6
    // The room a shadow needs around what casts it, which a surface of its own (the on-screen
    // display, the cards, the switcher) leaves so that its edge does not cut the shadow off;
    // none without effects.
    readonly property int shadowMargin: effects ? shadowBlur + shadowOffset : 0

    // The macOS style's popups. A token here that stands for one above is that token in the
    // taskbar style, so that a part uses it in both and the taskbar's looks as it always has.
    //
    // Text and icons on an accent fill: white, as macOS has them, unless the accent is too light.
    readonly property color textOnAccentFill: macos && luminance(accent) < 0.5 ? "#ffffff" : textOnAccent
    // A menu: its card's corners and the room inside its edge, a row and a section's heading
    // (in the caption's size, bold), the highlight's corners, where a row's label starts and how
    // far it is from an icon before it. The highlighted row is filled with the accent and its text
    // is white, and a check mark is in the text colour.
    readonly property int menuRadius: macos ? 8 : radiusMedium
    readonly property int menuPadding: macos ? 5 : spacingS
    readonly property int menuRowHeight: macos ? 24 : rowHeight
    readonly property int menuHeadingHeight: macos ? 24 : headingHeight
    readonly property int menuHeadingSize: macos ? fontSizeCaption : fontSizeSmall
    readonly property int menuRowRadius: macos ? 4 : radiusSmall
    readonly property int menuLabelInset: macos ? 9 : spacingL
    readonly property int menuIconGap: macos ? 7 : spacingL
    readonly property color menuHighlight: macos ? accent : hover
    readonly property color menuPressed: macos ? accentHover : pressed
    readonly property color menuHighlightText: macos ? textOnAccentFill : text
    readonly property color menuMark: macos ? text : accent
    // A popup's card: in a dark macOS appearance a little lighter than the bar, as macOS draws
    // menus and popovers over a dark window.
    readonly property color popupSurface: macos && !light ? mix(surface, Qt.rgba(1, 1, 1, 1), 0.035) : surface
    // A popup's outline, and in a dark macOS appearance a faint light line inside it, as macOS
    // edges a dark popup on a dark window.
    readonly property color popupOutline: macos && !light ? Qt.rgba(0, 0, 0, 0.6) : border
    readonly property color popupInnerEdge: macos && !light ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
    // Launchpad: its grid, columns by rows a page, and an application's icon there at most; the
    // scrim over the wallpaper behind it; its text, white in both appearances as on macOS; the
    // application the keyboard is at; its search field; and a dot for each page.
    readonly property int launchpadColumns: 7
    readonly property int launchpadRows: 5
    readonly property int launchpadIconSize: 96
    readonly property color launchpadScrim: Qt.rgba(0, 0, 0, light ? 0.28 : 0.42)
    readonly property color launchpadText: "#ffffff"
    readonly property color launchpadHighlight: Qt.rgba(1, 1, 1, 0.2)
    readonly property color launchpadField: Qt.rgba(1, 1, 1, 0.16)
    readonly property int launchpadFieldWidth: 240
    readonly property int launchpadFieldHeight: 28
    readonly property int launchpadDot: 7
    // Spotlight, the command palette of the macOS style: its card's corners; its field, without a
    // frame of its own, with the text and the magnifier large; a result's row and icon, the one
    // chosen filled with the accent; and a group's heading over its results.
    readonly property int spotlightRadius: macos ? 12 : radiusLarge
    readonly property int spotlightFieldHeight: 52
    readonly property int spotlightFontSize: Math.round(fontSize * 1.7)
    readonly property int spotlightGlyph: 22
    readonly property int spotlightRowHeight: 40
    readonly property int spotlightIconSize: 28
    readonly property int spotlightHeadingHeight: 26
    // Control Center, Quick Settings in the macOS style: its width and surface, and its modules,
    // cards lighter than the surface under them, with their corners, the room inside them, a
    // heading's height, a tile's height and its round button.
    readonly property int controlCenterWidth: 344
    readonly property color controlCenterSurface: macos && light ? mix(popupSurface, text, 0.05) : popupSurface
    readonly property color moduleColor: light ? Qt.rgba(1, 1, 1, 0.92) : mix(popupSurface, Qt.rgba(1, 1, 1, 1), 0.07)
    readonly property color moduleOutline: alpha(text, light ? 0.07 : 0.06)
    readonly property int moduleRadius: 12
    readonly property int modulePadding: 10
    readonly property int moduleHeadingHeight: 22
    readonly property int moduleTileHeight: 52
    readonly property int moduleButtonSize: 28
    // Notification Center, the clock's flyout in the macOS style, and its banners: how wide it is,
    // and a notification's icon beside it.
    readonly property int notificationCenterWidth: 344
    readonly property int notificationIconSize: 32
    // A switch's track while off, and the knob of a switch or a slider: white, as macOS has them,
    // with a faint outline that keeps it apart from a light surface.
    readonly property color switchTrack: alpha(text, light ? 0.14 : 0.2)
    readonly property color knob: macos ? "#ffffff" : accent
    readonly property color knobOutline: Qt.rgba(0, 0, 0, light ? 0.14 : 0.3)
    // A slider's track and knob.
    readonly property int sliderTrack: macos ? 6 : 4
    readonly property int sliderKnob: macos ? 18 : 14
    // A framed button: its height and its face, white in a light macOS appearance and lighter than
    // the surface in a dark one, its outline, and the ring that says the keyboard is at it.
    readonly property int buttonHeight: macos ? 28 : rowHeight - spacingS
    readonly property color buttonFace: !macos ? surfaceRaised : light ? Qt.rgba(1, 1, 1, 1)
                                                                 : mix(popupSurface, Qt.rgba(1, 1, 1, 1), 0.16)
    readonly property color buttonFaceHover: !macos ? surfaceRaisedHover : mix(buttonFace, text, 0.05)
    readonly property color buttonOutline: !macos ? border : light ? Qt.rgba(0, 0, 0, 0.14) : Qt.rgba(1, 1, 1, 0.06)
    readonly property color focusRing: macos ? alpha(accent, 0.55) : accent
    readonly property int focusRingWidth: macos ? 3 : 2
    // A search field: its height and its fill.
    readonly property int fieldHeight: macos ? 28 : rowHeight + spacingS
    readonly property color fieldFill: macos ? alpha(text, light ? 0.06 : 0.1) : surfaceRaised
    // The window switcher, in the macOS style as macOS's application switcher: large icons on a
    // rounded, translucent card, the one chosen on a light square, its title only under them.
    readonly property int switcherIconSize: macos ? 96 : appIconSizeDisplay
    readonly property int switcherRadius: macos ? 24 : radiusLarge
    readonly property color switcherSurface: macos ? alpha(popupSurface, 0.88) : surface
    readonly property color switcherSelection: macos ? alpha(text, 0.16) : accentSubtle

    // `milliseconds` at the animation speed, 0 with animations off.
    function duration(milliseconds) { return Math.round(milliseconds * motionScale) }
    function alpha(color, opacity) { return Qt.rgba(color.r, color.g, color.b, color.a * opacity) }
    // `a` with `amount` (0 to 1) of `b` mixed in.
    function mix(a, b, amount) {
        return Qt.rgba(a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
                       a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount)
    }
    // WCAG's relative luminance and contrast ratio.
    function luminance(color) {
        function channel(c) {
            return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b)
    }
    function contrast(a, b) {
        var la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
}
