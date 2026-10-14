// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// A caption near the bottom of an output, as `paw tour` narrates itself: a card with a title, the
// keys that do what it tells of and a sentence under them, rising into view and fading away when
// its time is up. A new caption while one shows takes its place, the card easing to its new size.
Item {
    id: caption
    required property string outputName
    readonly property var model: shell.caption
    readonly property bool mine: model.active && model.output === outputName
    // How far it is shown, from 0 to 1: its opacity, and what is left of its rise.
    property alias progress: reveal.progress
    // True while it is visible, including the fade out; the view hides the surface after that.
    readonly property bool visibleNow: mine || progress > 0
    readonly property int margin: Math.max(Theme.spacingL, Theme.shadowMargin)
    readonly property int padding: Theme.spacingXL + Theme.spacingXS
    width: card.width + 2 * margin
    height: card.height + 2 * margin
    Reveal { id: reveal; shown: caption.mine; enterDuration: Theme.durationSlow; exitDuration: Theme.durationSlow }

    Item {
        id: card
        objectName: "captionCard"
        anchors.centerIn: parent
        width: Math.min(560, Math.max(320, column.implicitWidth + 2 * caption.padding))
        height: column.implicitHeight + 2 * caption.padding
        opacity: caption.progress
        transform: Translate { y: (1 - caption.progress) * Theme.spacingXL }
        Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        CardFrame { radius: Theme.radiusLarge; color: Theme.surface; outline: Theme.border }
        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: caption.padding
            spacing: Theme.spacingS
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingL
                Text {
                    objectName: "captionTitle"
                    Layout.fillWidth: true
                    text: caption.model.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle; font.family: Theme.fontFamily; font.weight: Font.DemiBold
                    textFormat: Text.PlainText; elide: Text.ElideRight
                }
                // The keys, on a cap of the accent's tint.
                Rectangle {
                    visible: caption.model.keys !== ""
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: keys.implicitWidth + 2 * Theme.spacingM
                    implicitHeight: keys.implicitHeight + Theme.spacingS
                    radius: Theme.radiusSmall
                    color: Theme.accentSubtle
                    Text {
                        id: keys
                        objectName: "captionKeys"
                        anchors.centerIn: parent
                        text: caption.model.keys
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily; font.weight: Font.Medium
                        textFormat: Text.PlainText
                    }
                }
            }
            Text {
                objectName: "captionBody"
                visible: text !== ""
                Layout.fillWidth: true
                Layout.maximumWidth: 560 - 2 * caption.padding
                text: caption.model.body
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                lineHeight: 1.15
            }
        }
    }
}
