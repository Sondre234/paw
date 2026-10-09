// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes

// What is playing, at the top of Quick Settings as on Windows 11 (Control Center's Now Playing
// module in the macOS style): the current media player's cover, the track's title and artist,
// the player's icon and name (a click brings its window forward), the position, and previous,
// play or pause and next. With several players, arrows beside the name step through them.
// `media` is the players' model (Media); `shown` says whether the flyout is open, the only time
// the position moves on, and it is read again as it opens.
Item {
    id: card
    required property var media
    property bool shown: false
    objectName: "quickMedia"
    // In the taskbar style a raised card of its own; in the macOS style the module around it is.
    readonly property real padding: Theme.macos ? 0 : Theme.spacingL
    readonly property real artSize: Theme.macos ? 52 : 64
    readonly property color backdrop: Theme.macos ? Theme.moduleColor : Theme.surfaceRaised
    implicitHeight: column.implicitHeight + 2 * padding
    // The position in milliseconds, as last worked out.
    property real position: 0
    function refresh() { position = media.position() }
    onShownChanged: if (shown) { media.refreshPosition(); refresh() }
    Connections {
        target: card.media
        function onPositionChanged() { card.refresh() }
    }
    Timer {
        interval: 500; repeat: true
        running: card.shown && card.visible && card.media.playing
        onTriggered: card.refresh()
    }
    // 83000 as 1:23, an hour or more as 1:02:03.
    function time(ms) {
        var seconds = Math.floor(Math.max(0, ms) / 1000)
        var hours = Math.floor(seconds / 3600), minutes = Math.floor(seconds / 60) % 60
        var rest = seconds % 60
        return (hours > 0 ? hours + ":" + (minutes < 10 ? "0" : "") : "") + minutes + ":" +
               (rest < 10 ? "0" : "") + rest
    }

    Rectangle {
        visible: !Theme.macos
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: card.backdrop
        border.color: Theme.border
    }
    // A control of the track: a frameless round button with its icon.
    component Control: FlatButton {
        id: control
        property string glyph
        property real glyphSize: Theme.iconSize
        Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
        radius: width / 2
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: control.glyph; size: control.glyphSize
                color: control.enabled ? Theme.text : Theme.textDisabled
            }
        }
    }
    ColumnLayout {
        id: column
        x: card.padding; y: card.padding
        width: card.width - 2 * card.padding
        spacing: Theme.spacingS
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingL
            // The cover, or a note where there is none; its corners are rounded by covering them
            // in the colour it sits on, which every renderer draws.
            Item {
                Layout.preferredWidth: card.artSize; Layout.preferredHeight: card.artSize
                Layout.alignment: Qt.AlignTop
                Rectangle {
                    anchors.fill: parent
                    visible: cover.status !== Image.Ready
                    radius: Theme.radiusSmall
                    color: Theme.selected
                    Icon { anchors.centerIn: parent; name: "music"; size: Theme.iconSizeLarge; color: Theme.textMuted }
                }
                Image {
                    id: cover
                    objectName: "quickMediaArt"
                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: card.media.art
                    sourceSize: Qt.size(2 * card.artSize, 2 * card.artSize)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
                Shape {
                    anchors.fill: parent
                    visible: cover.visible
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: card.backdrop
                        strokeColor: "transparent"
                        fillRule: ShapePath.OddEvenFill
                        // The square, and the rounded square inside it left uncovered.
                        PathSvg {
                            path: {
                                var s = card.artSize, r = Theme.radiusSmall, a = "A" + r + " " + r + " 0 0 1 "
                                return "M0 0H" + s + "V" + s + "H0Z M" + r + " 0H" + (s - r) + a + s + " " + r +
                                       "V" + (s - r) + a + (s - r) + " " + s + "H" + r + a + "0 " + (s - r) +
                                       "V" + r + a + r + " 0Z"
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 0
                Text {
                    objectName: "quickMediaTitle"
                    Layout.fillWidth: true
                    text: card.media.title || card.media.identity
                    textFormat: Text.PlainText; elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                }
                Text {
                    objectName: "quickMediaArtist"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: card.media.artist
                    textFormat: Text.PlainText; elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
                // The player, and the others to step to.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingXS
                    spacing: Theme.spacingXS
                    FlatButton {
                        id: playerButton
                        objectName: "quickMediaPlayer"
                        Layout.fillWidth: true; Layout.preferredHeight: Theme.iconSizeSmall + Theme.spacingM
                        Layout.leftMargin: -Theme.spacingS
                        leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                        enabled: card.media.canRaise
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "Show " + card.media.identity
                        onClicked: card.media.raise()
                        background: ButtonFill {
                            radius: Theme.radiusSmall
                            hovered: playerButton.hovered; pressed: playerButton.pressed
                        }
                        contentItem: RowLayout {
                            spacing: Theme.spacingS
                            Image {
                                id: playerIcon
                                Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                                visible: status === Image.Ready
                                source: "image://icons/" + shell.iconFor(card.media.desktopEntry || card.media.identity.toLowerCase())
                                sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: card.media.identity
                                textFormat: Text.PlainText; elide: Text.ElideRight
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                            }
                        }
                    }
                    Control {
                        objectName: "quickMediaPreviousPlayer"
                        visible: card.media.players.length > 1
                        Layout.preferredWidth: Theme.iconSizeSmall + Theme.spacingM
                        Layout.preferredHeight: Theme.iconSizeSmall + Theme.spacingM
                        glyph: "chevron-left"; glyphSize: Theme.iconSizeSmall
                        text: "Previous player"
                        onClicked: card.media.selectNext(-1)
                    }
                    Text {
                        visible: card.media.players.length > 1
                        text: (card.media.index + 1) + "/" + card.media.players.length
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                    }
                    Control {
                        objectName: "quickMediaNextPlayer"
                        visible: card.media.players.length > 1
                        Layout.preferredWidth: Theme.iconSizeSmall + Theme.spacingM
                        Layout.preferredHeight: Theme.iconSizeSmall + Theme.spacingM
                        glyph: "chevron-right"; glyphSize: Theme.iconSizeSmall
                        text: "Next player"
                        onClicked: card.media.selectNext(1)
                    }
                }
            }
        }
        // Where the track is, where the player says how long it is; a press or a drag moves it
        // there when the player can seek.
        ColumnLayout {
            Layout.fillWidth: true
            visible: card.media.length > 0
            spacing: 0
            AudioSlider {
                id: seeker
                objectName: "quickMediaPosition"
                Layout.fillWidth: true; Layout.preferredHeight: Theme.iconSize
                from: 0; to: Math.max(1, card.media.length); stepSize: 0
                enabled: card.media.canSeek
                Accessible.name: "Position"
                onPressedChanged: if (!pressed) card.media.seek(value)
                Binding on value {
                    when: !seeker.pressed
                    value: card.position
                    restoreMode: Binding.RestoreNone
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "quickMediaElapsed"
                    Layout.fillWidth: true
                    text: card.time(seeker.pressed ? seeker.value : card.position)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                    font.features: { "tnum": 1 }
                }
                Text {
                    text: card.time(card.media.length)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                    font.features: { "tnum": 1 }
                }
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingL
            Control {
                objectName: "quickMediaPrevious"
                glyph: "skip-back"
                text: "Previous"
                enabled: card.media.canGoPrevious
                onClicked: card.media.previous()
            }
            Control {
                objectName: "quickMediaPlayPause"
                Layout.preferredWidth: Theme.rowHeight + Theme.spacingS; Layout.preferredHeight: Theme.rowHeight + Theme.spacingS
                glyph: card.media.playing ? "pause" : "play"; glyphSize: Theme.iconSizeLarge
                text: card.media.playing ? "Pause" : "Play"
                enabled: card.media.canPlayPause
                onClicked: card.media.playPause()
            }
            Control {
                objectName: "quickMediaNext"
                glyph: "skip-forward"
                text: "Next"
                enabled: card.media.canGoNext
                onClicked: card.media.next()
            }
        }
    }
}
