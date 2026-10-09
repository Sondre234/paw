// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// The wallpaper the macOS style draws while none is set: a soft composition of its own, a sky in the
// desktop's background colour with two glows in it and three rolling hills in hues around the
// accent, each lit along its crest; light or dark as the appearance is. It is only shapes and
// gradients, so that it draws with the software renderer too, at any size.
Item {
    id: drawn
    // The hues it is made of, in turns: the background's, unless that is grey, and the accent's.
    readonly property real skyHue: hueOf(shell.background, accentHue)
    readonly property real accentHue: hueOf(Theme.accent, 0.6)
    function hueOf(color, otherwise) {
        return color.hslSaturation > 0.12 && color.hslHue >= 0 ? color.hslHue : otherwise
    }
    // A colour of hue `hue` (turns, any number of them), saturation `saturation` and lightness
    // `light` in the light appearance or `dark` in the dark one, at `alpha`.
    function tone(hue, saturation, light, dark, alpha) {
        return Qt.hsla(hue - Math.floor(hue), saturation, Theme.light ? light : dark, alpha === undefined ? 1 : alpha)
    }
    readonly property real reach: Math.max(width, height)

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: drawn.tone(drawn.skyHue, 0.6, 0.88, 0.1) }
            GradientStop { position: 1; color: drawn.tone(drawn.skyHue + 0.04, 0.55, 0.76, 0.19) }
        }
    }

    // A glow: a colour fading out from `centre` (a fraction of the size) over `radius` (of the
    // longer side).
    component Glow: Shape {
        id: glow
        property point centre
        property real radius
        property color color
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.centre.x * drawn.width; centerY: glow.centre.y * drawn.height
                focalX: centerX; focalY: centerY
                centerRadius: glow.radius * drawn.reach
                GradientStop { position: 0; color: glow.color }
                GradientStop { position: 1; color: Qt.rgba(glow.color.r, glow.color.g, glow.color.b, 0) }
            }
            PathRectangle { width: drawn.width; height: drawn.height }
        }
    }
    Glow {
        centre: Qt.point(0.8, 0.12); radius: 0.62
        color: drawn.tone(drawn.accentHue + 0.14, 0.85, 0.84, 0.46, 0.75)
    }
    Glow {
        centre: Qt.point(0.08, 0.38); radius: 0.45
        color: drawn.tone(drawn.accentHue - 0.08, 0.75, 0.86, 0.36, 0.55)
    }

    // A hill across the whole width, from `from` on the left edge to `to` on the right (fractions
    // of the height) by two control points, shaded from `crest` down to `base`, with a line of light
    // along its crest in a soft glow.
    component Hill: Shape {
        id: hill
        property real from
        property real to
        property point control1
        property point control2
        property color crest
        property color base
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        readonly property real highest: Math.min(from, to, control1.y, control2.y) * drawn.height
        readonly property color light: Qt.rgba(1, 1, 1, Theme.light ? 0.55 : 0.2)
        // Past the edges, so that the lines along them do not show.
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: hill.highest
                x2: 0; y2: drawn.height
                GradientStop { position: 0; color: hill.crest }
                GradientStop { position: 1; color: hill.base }
            }
            startX: -2; startY: hill.from * drawn.height
            PathCubic {
                x: drawn.width + 2; y: hill.to * drawn.height
                control1X: hill.control1.x * drawn.width; control1Y: hill.control1.y * drawn.height
                control2X: hill.control2.x * drawn.width; control2Y: hill.control2.y * drawn.height
            }
            PathLine { x: drawn.width + 2; y: drawn.height + 2 }
            PathLine { x: -2; y: drawn.height + 2 }
            PathLine { x: -2; y: hill.from * drawn.height }
        }
        // The glow: the crest drawn wide and faint, twice, under the line.
        ShapePath {
            fillColor: "transparent"
            strokeColor: Qt.rgba(hill.light.r, hill.light.g, hill.light.b, hill.light.a * 0.12)
            strokeWidth: 0.04 * drawn.reach
            capStyle: ShapePath.FlatCap
            PathSvg { path: crestPath.path }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: Qt.rgba(hill.light.r, hill.light.g, hill.light.b, hill.light.a * 0.2)
            strokeWidth: 0.012 * drawn.reach
            capStyle: ShapePath.FlatCap
            PathSvg { path: crestPath.path }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: hill.light
            strokeWidth: 1.5
            PathSvg {
                id: crestPath
                path: "M -2 " + hill.from * drawn.height +
                      " C " + hill.control1.x * drawn.width + " " + hill.control1.y * drawn.height +
                      " " + hill.control2.x * drawn.width + " " + hill.control2.y * drawn.height +
                      " " + (drawn.width + 2) + " " + hill.to * drawn.height
            }
        }
    }
    Hill {
        from: 0.5; to: 0.3
        control1: Qt.point(0.3, 0.28); control2: Qt.point(0.6, 0.64)
        crest: drawn.tone(drawn.accentHue + 0.11, 0.62, 0.8, 0.38)
        base: drawn.tone(drawn.accentHue + 0.15, 0.55, 0.64, 0.18)
    }
    Hill {
        from: 0.6; to: 0.66
        control1: Qt.point(0.28, 0.84); control2: Qt.point(0.66, 0.4)
        crest: drawn.tone(drawn.accentHue, 0.74, 0.72, 0.44)
        base: drawn.tone(drawn.accentHue + 0.03, 0.68, 0.55, 0.15)
    }
    Hill {
        from: 0.86; to: 0.8
        control1: Qt.point(0.44, 0.7); control2: Qt.point(0.76, 1.02)
        crest: drawn.tone(drawn.accentHue - 0.08, 0.66, 0.68, 0.32)
        base: drawn.tone(drawn.accentHue - 0.05, 0.6, 0.48, 0.1)
    }
}
