// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import Paw

// The card of the workspace of the indicator the pointer rests on, with shell.thumbnails (the
// panel's workspaceCardOpen): above it (below a bar along the top), as the card of a button's
// windows opens, a picture of the workspace under its name, its wallpaper and its windows where
// they are, drawn in their order from their pictures (WorkspaceWindows.qml), at the output's
// proportions. It is made from halfway into the delay (the panel's workspaceWarm), so that it
// opens on the pictures, glides to the next workspace the pointer rests on while the windows of
// the one crossfade into those of the other, and keeps what it showed while it fades out. The
// workspace shown on the monitor is ringed in the accent colour. The pointer on a window rings it
// and names it in the header; a click on it brings the window up, and one elsewhere on the card
// shows the workspace.
PopupCard {
    id: card
    required property var panel
    parent: panel.popupLayer
    objectName: "workspaceCard"
    open: panel.workspaceCardOpen
    // Shown on hover, it leaves the keyboard where it is.
    initialFocus: null
    anchorRect: panel.dockAnchor(panel.workspaceCardX, 0)
    side: panel.dockSide
    bounds: panel.popupArea
    // Open, it glides to another workspace's place.
    glides: true
    readonly property real tileGap: panel.thumbnailGap
    readonly property real padding: panel.thumbnailPadding
    readonly property real headerHeight: Theme.iconSize + Theme.spacingS
    readonly property size miniature: panel.workspaceMiniature
    implicitWidth: 2 * tileGap + 2 * padding + miniature.width
    implicitHeight: 2 * tileGap + 2 * padding + headerHeight + Theme.spacingS + miniature.height
    HoverHandler {
        id: hover
        onHoveredChanged: card.panel.hoverWorkspaceCard(hovered)
    }
    readonly property bool hovered: hover.hovered
    // The picture of the workspace, which a window dragged onto the workspace drops into.
    readonly property Item miniatureItem: picture

    // The workspace it shows: the one open, else the one warmed up, kept while it fades out; and
    // whether its windows are watched, from the warm-up until it has faded out.
    property int number: 0
    readonly property int wanted: panel.workspaceCardOpen ? panel.workspaceCardNumber : panel.workspaceWarm
    onWantedChanged: if (wanted > 0) show(wanted)
    Component.onCompleted: if (wanted > 0) show(wanted)
    readonly property bool live: visible || panel.workspaceWarm > 0
    readonly property var workspaceState: shell.workspaces[panel.outputName] || ({ current: 1 })
    readonly property bool current: number === workspaceState.current
    readonly property string label: shell.workspaceNames[number - 1] || ("Workspace " + number)

    // Two layers of windows, the one in front showing `number`: another workspace goes into the
    // one behind, which comes forward once its windows are listed (or a moment later), the two
    // crossfading; the one going lets its workspace go once it has faded out. Shown, the card
    // takes another workspace at once.
    property Item front: first
    readonly property Item back: front === first ? second : first
    function show(next) {
        number = next
        if (front.number === next) {
            swapping.stop()
            return
        }
        if (front.number <= 0 || !visible || Theme.durationNormal === 0) {
            swapping.stop()
            front.number = next
            back.number = 0
            return
        }
        back.number = next
        if (back.known) swap()
        else swapping.restart()
    }
    function swap() {
        swapping.stop()
        front = back
    }
    Timer { id: swapping; interval: 150; onTriggered: card.swap() }
    Connections {
        target: card.back
        function onKnownChanged() { if (swapping.running && card.back.known) card.swap() }
    }
    // The workspace, or the window clicked.
    function pick(window) {
        panel.pickWorkspace(number, window)
    }

    Button {
        id: tile
        objectName: "workspaceTile"
        anchors.fill: parent; anchors.margins: card.tileGap
        padding: card.padding
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: card.label
        onClicked: card.pick(card.front.hoveredWindow)
        background: ButtonFill {
            hovered: tile.hovered
            pressed: tile.pressed
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacingS
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: card.headerHeight
                spacing: Theme.spacingS
                Icon {
                    Layout.leftMargin: Theme.spacingXS
                    name: "layout-grid"
                    size: Theme.iconSizeSmall
                    color: card.current ? Theme.accent : Theme.textMuted
                }
                Text {
                    objectName: "workspaceCardTitle"
                    text: card.label; textFormat: Text.PlainText; elide: Text.ElideRight
                    Layout.maximumWidth: card.miniature.width / 2
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    font.weight: card.current ? Font.DemiBold : Font.Normal
                }
                Item { Layout.fillWidth: true }
                // The window under the pointer, else how many windows there are.
                Image {
                    readonly property var window: card.front.hoveredWindow
                    visible: !!window
                    Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                    source: window ? shell.iconBase + shell.iconFor(window.appId || "") : ""
                    sourceSize: Qt.size(Theme.iconSizeSmall, Theme.iconSizeSmall)
                }
                Text {
                    objectName: "workspaceCardDetail"
                    readonly property var window: card.front.hoveredWindow
                    Layout.maximumWidth: card.miniature.width / 2
                    Layout.rightMargin: Theme.spacingXS
                    text: window ? window.title
                        : !card.front.known ? ""
                        : card.front.count === 0 ? "Empty"
                        : card.front.count === 1 ? "1 window" : card.front.count + " windows"
                    textFormat: Text.PlainText; elide: Text.ElideRight
                    color: window ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
            }
            // The picture: the wallpaper as the desktop draws it, the windows over it; with the
            // GPU its corners are rounded.
            Item {
                id: picture
                objectName: "workspaceMiniature"
                Layout.preferredWidth: card.miniature.width; Layout.preferredHeight: card.miniature.height
                clip: !Theme.effects
                Item {
                    id: painted
                    anchors.fill: parent
                    layer.enabled: Theme.effects
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: pictureMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: shell.background
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: shell.wallpaper.toString().length === 0 || wallpaper.status === Image.Error
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.lighter(shell.background, 1.45) }
                            GradientStop { position: 1; color: shell.background }
                        }
                    }
                    WallpaperPicture {
                        id: wallpaper
                        anchors.fill: parent
                        source: shell.wallpaper
                        // A pixel of a picture at its own size is one of the output's, drawn as
                        // small as the card draws the output.
                        pixelScale: card.panel.workspaceScale / Screen.devicePixelRatio
                        sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio),
                                            Math.ceil(height * Screen.devicePixelRatio))
                        asynchronous: true
                    }
                    WorkspaceWindows {
                        id: first
                        objectName: "workspaceWindowsFirst"
                        anchors.fill: parent
                        panel: card.panel
                        live: card.live
                        factor: card.panel.workspaceScale
                        opacity: card.front === first ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { enabled: card.visible; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        onOpacityChanged: if (opacity === 0 && card.front !== first) number = 0
                    }
                    WorkspaceWindows {
                        id: second
                        objectName: "workspaceWindowsSecond"
                        anchors.fill: parent
                        panel: card.panel
                        live: card.live
                        factor: card.panel.workspaceScale
                        opacity: card.front === second ? 1 : 0
                        visible: opacity > 0
                        Behavior on opacity { enabled: card.visible; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        onOpacityChanged: if (opacity === 0 && card.front !== second) number = 0
                    }
                }
                Item {
                    id: pictureMask
                    anchors.fill: parent
                    visible: false
                    layer.enabled: Theme.effects
                    Rectangle { anchors.fill: parent; radius: Theme.radiusSmall }
                }
                // Ringed in the accent colour while the monitor shows it, else edged.
                Rectangle {
                    objectName: "workspaceMiniatureRing"
                    anchors.fill: parent
                    color: "transparent"
                    radius: Theme.effects ? Theme.radiusSmall : 0
                    border.width: card.current ? 2 : 1
                    border.color: card.current ? Theme.accent : Theme.border
                    Behavior on border.color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                }
            }
        }
    }
}
