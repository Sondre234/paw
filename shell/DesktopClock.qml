// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// The desktop clock (shell.desktop_clock, experimental): the time, large and thin, the date under
// it and, with `greeting`, a word for the time of day above it between two twinkles in the accent
// colour, where `position` puts it in `area` (the desktop less its bars). A soft shadow, or a glow
// under dark ink, sets it apart from any wallpaper. It changes once a minute and only then, as the
// blur behind windows blurs the desktop again whenever it changes; and Desktop.qml draws it outside
// the backdrop the frosted glass takes, which stays the wallpaper alone.
Item {
    id: clock
    objectName: "desktopClock"
    // The room it may take, in the desktop's coordinates.
    property rect area
    readonly property var model: shell.desktopClock
    property date now: new Date()
    readonly property int size: model.size
    readonly property color ink: model.color
    readonly property string family: model.font.length > 0 ? model.font : Theme.fontFamily
    // Light ink casts a shadow, dark ink a glow: of the background colour where that stands apart
    // from the ink, else of black or white.
    readonly property bool lightInk: Theme.luminance(ink) > 0.179 // nearer white than black
    readonly property color shade: Theme.contrast(ink, shell.background) >= 3
                                   ? Theme.alpha(shell.background, lightInk ? 0.85 : 0.9)
                                   : lightInk ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.8)
    // Which side `position` puts it on, across and down: -1, 0 or 1.
    readonly property int across: model.position.endsWith("left") ? -1 : model.position.endsWith("right") ? 1 : 0
    readonly property int down: model.position.startsWith("top") ? -1 : model.position.startsWith("bottom") ? 1 : 0
    readonly property real margin: Math.round(Math.max(48, size / 2))

    opacity: model.shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }

    // It shows minutes, so it wakes once a minute, just after the minute changes, while shown.
    Timer {
        id: tick
        objectName: "desktopClockTick"
        function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
        interval: untilMinute()
        running: clock.model.shown
        repeat: true
        // Shown again after a while, it tells the time anew.
        onRunningChanged: if (running) { clock.now = new Date(); interval = untilMinute() }
        onTriggered: { clock.now = new Date(); interval = untilMinute() }
    }

    Column {
        id: face
        objectName: "desktopClockFace"
        x: clock.across < 0 ? clock.area.x + clock.margin
         : clock.across > 0 ? clock.area.x + clock.area.width - clock.margin - width
         : clock.area.x + Math.round((clock.area.width - width) / 2)
        y: clock.down < 0 ? clock.area.y + clock.margin
         : clock.down > 0 ? clock.area.y + clock.area.height - clock.margin - height
         : clock.area.y + Math.round((clock.area.height - height) / 2)
        spacing: Math.round(clock.size * 0.14)
        layer.enabled: Theme.effects && clock.visible
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: clock.shade
            shadowBlur: 1
            blurMax: Math.max(16, Math.round(clock.size / 3))
            shadowVerticalOffset: clock.lightInk ? Math.max(1, Math.round(clock.size / 48)) : 0
            autoPaddingEnabled: true
        }
        // Where a line goes across: on the side the clock is on.
        function along(width) {
            return clock.across < 0 ? 0 : clock.across > 0 ? face.width - width : Math.round((face.width - width) / 2)
        }
        component Line: Text {
            textFormat: Text.PlainText
            font.family: clock.family
            // Drawn in software, without the shadow, a raised edge sets it apart instead.
            style: Theme.effects ? Text.Normal : Text.Raised
            styleColor: clock.shade
        }
        // A twinkle: a four-pointed star with curved sides, as a wallpaper's stars are drawn.
        component Twinkle: Shape {
            width: Math.round(greeting.font.pixelSize * 0.55); height: width
            anchors.verticalCenter: parent.verticalCenter
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: Theme.alpha(Theme.accent, 0.85)
                strokeWidth: -1
                scale: Qt.size(width / 24, height / 24)
                PathSvg { path: "M 12 0 Q 13.4 10.6 24 12 Q 13.4 13.4 12 24 Q 10.6 13.4 0 12 Q 10.6 10.6 12 0 Z" }
            }
        }
        Row {
            x: face.along(width)
            visible: greeting.text.length > 0
            spacing: Math.round(greeting.font.pixelSize * 0.45)
            Twinkle {}
            Line {
                id: greeting
                objectName: "desktopClockGreeting"
                text: clock.model.greeting.length > 0 ? clock.model.greetingAt(clock.now.getHours()) : ""
                color: Theme.accent
                font.pixelSize: Math.round(clock.size * 0.24)
                font.letterSpacing: Math.round(clock.size * 0.01)
            }
            Twinkle {}
        }
        // The time, in a box as tall as its digits and as wide as its ink, so that it lines up
        // with the edges and the lines around it by what is drawn rather than by the font's room.
        Item {
            id: timeBox
            readonly property rect ink: metrics.tightBoundingRect(time.text)
            readonly property rect digits: metrics.tightBoundingRect("0123456789")
            x: face.along(width)
            width: Math.ceil(ink.width)
            height: Math.ceil(digits.height)
            FontMetrics { id: metrics; font: time.font }
            Line {
                id: time
                objectName: "desktopClockTime"
                x: -Math.round(timeBox.ink.x)
                y: -Math.round(metrics.ascent + timeBox.digits.y)
                text: Qt.formatDateTime(clock.now, clock.model.timeFormat)
                color: clock.ink
                font.pixelSize: clock.size
                font.weight: Font.Light
                font.letterSpacing: -Math.round(clock.size * 0.02)
            }
        }
        Line {
            objectName: "desktopClockDate"
            x: face.along(width)
            visible: text.length > 0
            text: clock.model.dateFormat.length > 0 ? Qt.formatDateTime(clock.now, clock.model.dateFormat) : ""
            color: Theme.alpha(clock.ink, 0.9)
            font.pixelSize: Math.max(10, Math.round(clock.size * 0.17))
            font.letterSpacing: Math.round(clock.size * 0.02)
        }
    }
}
