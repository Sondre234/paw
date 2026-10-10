// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects
import QtQuick.Layouts

// The on-screen display: a pill with an icon, a label and a level, rising into view from the edge
// it is by and fading away when its time is up.
Item {
    id: osd
    required property string outputName
    readonly property var model: shell.osd
    readonly property bool mine: model.active && model.output === outputName
    // How far it is shown, from 0 to 1: its opacity, and what is left of its rise and growth. It
    // comes quickly and goes slowly, both at once with animations off.
    property alias progress: reveal.progress
    // True while it is visible, including the fade out; the view hides the surface after that.
    readonly property bool visibleNow: mine || progress > 0
    readonly property bool hasLevel: model.percent >= 0
    // The room around the pill, which its shadow takes when there is one.
    readonly property int margin: Math.max(Theme.spacingL, Theme.shadowMargin)
    width: pill.width + 2 * margin
    height: pill.height + 2 * margin
    Reveal { id: reveal; shown: osd.mine; exitDuration: Theme.durationSlow }

    Item {
        id: pill
        objectName: "osdPill"
        anchors.centerIn: parent
        // With a level it keeps its width as the level changes; a message alone fits its text.
        width: osd.hasLevel ? 300 : Math.min(300, row.implicitWidth + row.anchors.leftMargin + row.anchors.rightMargin)
        height: Theme.rowHeight + 2 * Theme.spacingM
        opacity: osd.progress
        scale: 0.94 + 0.06 * osd.progress
        transform: Translate { y: (1 - osd.progress) * Theme.spacingM * (osd.model.top ? -1 : 1) }
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: pill.height / 2
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.surface
            border.color: Theme.border
        }
        RowLayout {
            id: row
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingXL
            anchors.rightMargin: Theme.spacingXL + Theme.spacingXS
            spacing: Theme.spacingL
            // What it is about: the loudspeaker for the volume, as the bar draws it, a sun for
            // the brightness, a microphone, the bell for notifications, else a mark for a
            // message.
            Item {
                id: glyph
                readonly property string kind: osd.model.kind
                readonly property bool speaker: kind === "volume" || kind === "muted"
                Layout.preferredWidth: Theme.iconSizeLarge; Layout.preferredHeight: Theme.iconSizeLarge
                SpeakerIcon {
                    visible: glyph.speaker
                    size: Theme.iconSizeLarge
                    level: osd.model.percent
                    muted: glyph.kind === "muted"
                }
                Icon {
                    visible: !glyph.speaker
                    size: Theme.iconSizeLarge
                    name: glyph.kind === "brightness" ? "sun" : glyph.kind === "dnd" ? "bell-off"
                          : glyph.kind === "notifications" ? "bell"
                          : glyph.kind === "microphone" ? "mic"
                          : glyph.kind === "microphone-muted" ? "mic-off" : "info"
                }
            }
            Text {
                id: label
                objectName: "osdText"
                Layout.fillWidth: !osd.hasLevel
                Layout.maximumWidth: osd.hasLevel ? 110 : -1
                text: osd.model.text
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge; font.family: Theme.fontFamily; font.weight: Font.Medium
                textFormat: Text.PlainText; elide: Text.ElideRight
            }
            Rectangle {
                id: track
                objectName: "osdLevel"
                visible: osd.hasLevel
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.spacingS + Theme.spacingXS
                radius: height / 2
                color: Theme.selected
                // The level glides to a new value, and on from there when keys repeat.
                Rectangle {
                    width: parent.width * Math.max(0, osd.model.percent) / 100; height: parent.height
                    radius: height / 2
                    color: osd.model.kind === "muted" ? Theme.textMuted : Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                }
            }
            // The number in figures of one width, in room for 100, so that it does not shift as
            // it changes.
            Text {
                visible: osd.hasLevel
                Layout.preferredWidth: digits.width
                horizontalAlignment: Text.AlignRight
                text: osd.model.percent
                color: Theme.textMuted
                font: digits.font
                TextMetrics {
                    id: digits
                    text: "100"
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                    font.features: { "tnum": 1 }
                }
            }
        }
    }
}
