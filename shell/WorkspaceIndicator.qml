// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// This output's workspaces: the current one on a pill that slides from workspace to workspace,
// a dot under those with windows. Clicking a number switches to it; scrolling pages through
// them, as it does anywhere on the bar. With shell.workspacesShown set, only that many show,
// around the current one. With the panel's workspaceCards, resting on one shows a picture of it
// (WorkspaceCard.qml) in place of its tooltip.
Item {
    id: workspaceIndicator
    required property var panel
    objectName: "workspaceIndicator"
    readonly property var workspaceState: shell.workspaces[workspaceIndicator.panel.outputName] || ({ current: 1, occupied: [] })
    // The workspaces shown, first to first + shown - 1: the current one in the middle, except
    // near either end, where the range stops at the first or last workspace.
    readonly property int shown: shell.workspacesShown > 0 ? Math.min(shell.workspacesShown, shell.workspaceCount) : shell.workspaceCount
    readonly property int first: Math.max(1, Math.min(shell.workspaceCount - shown + 1, workspaceState.current - Math.floor((shown - 1) / 2)))
    function show(number) {
        if (number >= 1 && number <= shell.workspaceCount && number !== workspaceState.current)
            shell.showWorkspace(workspaceIndicator.panel.outputName, number)
    }
    visible: shell.workspaceCount > 1 && shell.widgets.workspaces
    implicitWidth: buttons.implicitWidth; implicitHeight: buttons.implicitHeight
    Layout.alignment: Qt.AlignCenter
    // Behind the buttons, under the current workspace's: it slides to the next one and takes its
    // width (its height, down a side).
    Rectangle {
        id: pill
        objectName: "workspacePill"
        // Read again as the buttons are made anew.
        readonly property Item target: repeater.count > 0 ? repeater.itemAt(workspaceIndicator.workspaceState.current - workspaceIndicator.first) : null
        visible: target !== null
        x: target ? target.x : 0
        y: target ? target.y : 0
        width: target ? target.width : 0
        height: target ? target.height : 0
        radius: Theme.radiusSmall
        color: Theme.selected
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    }
    // In a row, or a column along a side.
    Grid {
        id: buttons
        spacing: Theme.spacingXS
        columns: Theme.barVertical ? 1 : Math.max(1, repeater.count)
        Repeater {
            id: repeater
            model: workspaceIndicator.shown
            delegate: FlatButton {
                id: workspaceButton
                required property int index
                readonly property int number: workspaceIndicator.first + index
                readonly property bool current: workspaceIndicator.workspaceState.current === number
                readonly property bool occupied: workspaceIndicator.workspaceState.occupied.indexOf(number) >= 0
                // A window on this workspace is asking for attention.
                readonly property bool urgent: (workspaceIndicator.workspaceState.urgent || []).indexOf(number) >= 0
                readonly property string label: shell.workspaceNames[number - 1] || ""
                objectName: "workspace" + number
                // A number with room around it, or a name with a little more; along a side as wide
                // as the bar leaves, a name cut short.
                readonly property real narrowest: Theme.iconSize + Theme.spacingM
                width: Theme.barVertical ? Theme.barButtonHeight - Theme.spacingS
                     : label ? Math.max(narrowest, workspaceText.implicitWidth + 2 * Theme.spacingM) : narrowest
                height: Theme.barVertical ? narrowest : Theme.barButtonHeight - Theme.spacingS
                onClicked: { workspaceIndicator.panel.closeMenus(); workspaceIndicator.show(number) }
                // Resting on it shows its card, whatever the platform thinks of hover.
                hoverEnabled: true
                onHoveredChanged: workspaceIndicator.panel.hoverWorkspace(workspaceButton, hovered)
                onPressedChanged: if (pressed) workspaceIndicator.panel.closeWorkspaceCard()
                // A window dragged from the bar drops onto it (the panel's workspaceAt): while the
                // drag is over it, it is lit and grows a little; as windows drop onto it, it pops.
                readonly property bool dropsWindows: true
                readonly property bool dropTarget: workspaceIndicator.panel.dropWorkspace === number
                property real popped: 1
                scale: (dropTarget ? 1.12 : 1) * popped
                Behavior on scale { enabled: !pop.running; NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }
                SequentialAnimation {
                    id: pop
                    NumberAnimation { target: workspaceButton; property: "popped"; to: 1.25; duration: Theme.durationFast; easing.type: Theme.easing }
                    NumberAnimation { target: workspaceButton; property: "popped"; to: 1; duration: Theme.durationSlow; easing.type: Easing.OutBack }
                }
                Connections {
                    target: workspaceIndicator.panel
                    function onWindowsDropped(number) { if (number === workspaceButton.number && Theme.durationFast > 0) pop.restart() }
                }
                background: ButtonFill {
                    radius: workspaceButton.radius
                    hovered: workspaceButton.hovered
                    pressed: workspaceButton.pressed
                    active: workspaceButton.active
                    color: workspaceButton.dropTarget ? Theme.accentSubtle : stateColor
                    Rectangle {
                        objectName: "workspaceDropRing"
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: 2; border.color: Theme.accent
                        opacity: workspaceButton.dropTarget ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    }
                }
                Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
                BarTip {
                    panel: workspaceIndicator.panel; owner: workspaceButton
                    visible: !workspaceIndicator.panel.workspaceCards && owner.hovered && !owner.pressed && !panel.menuOpen
                    text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "")
                }
                contentItem: Item {
                    Text {
                        id: workspaceText
                        anchors.centerIn: parent
                        width: Theme.barVertical ? Math.min(implicitWidth, parent.width - Theme.spacingXS) : implicitWidth
                        elide: Text.ElideRight
                        text: workspaceButton.label ? workspaceButton.label : workspaceButton.number
                        color: workspaceButton.urgent && !workspaceButton.current ? Theme.urgent : workspaceButton.current ? Theme.accent : Theme.text
                        Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                        font.weight: workspaceButton.current ? Font.DemiBold : Font.Normal
                    }
                    Rectangle {
                        objectName: "workspaceUrgent" + workspaceButton.number
                        visible: workspaceButton.urgent
                        anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: Theme.spacingXS
                        width: 6; height: 6; radius: 3
                        color: Theme.urgent
                        SequentialAnimation on opacity {
                            running: workspaceButton.urgent
                            loops: 6; alwaysRunToEnd: true
                            NumberAnimation { to: 0.3; duration: Theme.duration(450) }
                            NumberAnimation { to: 1; duration: Theme.duration(450) }
                        }
                    }
                    Rectangle {
                        visible: workspaceButton.occupied
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom
                        width: 4; height: 4; radius: 2
                        color: workspaceButton.current ? Theme.accent : Theme.text
                        Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                    }
                }
            }
        }
    }
}
