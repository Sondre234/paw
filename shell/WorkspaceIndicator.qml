// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// This output's workspaces: the current one on a pill that slides from workspace to workspace,
// a dot under those with windows. Clicking a number switches to it; scrolling pages through
// them, as it does anywhere on the bar. With shell.workspacesShown set, only that many show,
// around the current one.
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
    Layout.alignment: Qt.AlignVCenter
    // Behind the buttons, under the current workspace's: it slides to the next one and takes its
    // width.
    Rectangle {
        id: pill
        objectName: "workspacePill"
        // Read again as the buttons are made anew.
        readonly property Item target: repeater.count > 0 ? repeater.itemAt(workspaceIndicator.workspaceState.current - workspaceIndicator.first) : null
        visible: target !== null
        x: target ? target.x : 0
        width: target ? target.width : 0
        height: buttons.height
        radius: Theme.radiusSmall
        color: Theme.selected
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    }
    Row {
        id: buttons
        spacing: Theme.spacingXS
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
                // A number with room around it, or a name with a little more.
                readonly property real narrowest: Theme.iconSize + Theme.spacingM
                width: label ? Math.max(narrowest, workspaceText.implicitWidth + 2 * Theme.spacingM) : narrowest
                height: Theme.barButtonHeight - Theme.spacingS
                onClicked: { workspaceIndicator.panel.closeMenus(); workspaceIndicator.show(number) }
                Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
                BarTip { panel: workspaceIndicator.panel; owner: workspaceButton; text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "") }
                contentItem: Item {
                    Text {
                        id: workspaceText
                        anchors.centerIn: parent
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
