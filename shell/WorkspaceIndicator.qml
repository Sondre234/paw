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
    // A step past the last workspace (`way` 1) or the first (-1): the compositor nudges the
    // windows toward the one that is not there and back, and the pill bumps that way with them.
    function nudge(way) {
        shell.nudgeWorkspace(workspaceIndicator.panel.outputName, way)
        pill.bump(way)
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
        // Bumped along the bar by nudge(): out quickly, then back, as the windows are. Bumped again
        // on its way out the same way, it goes on as it is.
        property real bumped: 0
        property int bumpWay: 0 // the way it is on its way out, else 0
        function bump(way) {
            if (bumping.running && bumpWay === way)
                return
            bumpWay = way
            bumpOut.to = way * Theme.spacingXS * 1.5
            bumping.restart()
        }
        transform: Translate {
            x: Theme.barVertical ? 0 : pill.bumped
            y: Theme.barVertical ? pill.bumped : 0
        }
        SequentialAnimation {
            id: bumping
            objectName: "workspacePillBump"
            NumberAnimation { id: bumpOut; target: pill; property: "bumped"; duration: Theme.duration(80); easing.type: Easing.OutQuad }
            ScriptAction { script: pill.bumpWay = 0 }
            NumberAnimation { target: pill; property: "bumped"; to: 0; duration: Theme.duration(160); easing.type: Easing.InOutQuad }
        }
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
                Accessible.name: "Workspace " + number + (label ? " " + label : "") + (urgent ? " (needs attention)" : "")
                BarTip { panel: workspaceIndicator.panel; owner: workspaceButton; text: "Workspace " + number + (label ? ": " + label : "") + (occupied ? "" : " (empty)") + (urgent ? ", needs attention" : "") }
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
