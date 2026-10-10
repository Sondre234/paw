// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Paw

// The windows of a hovered stacked button (or, with shell.thumbnails, of one with more windows
// than their pictures fit across the output): clicking one focuses it (or minimizes it when
// focused already), the cross or a middle click closes it, and a right click opens its menu.
// Its rows are a menu's, the focused window's marked as the bar marks it: selected, with a line
// in the accent colour (the urgent one for a window asking for attention) at its start.
PopupCard {
    id: groupList
    required property var panel
    parent: panel.popupLayer
    objectName: "groupList"
    readonly property real rowHeight: Theme.rowHeight + Theme.spacingS
    readonly property real padding: Theme.spacingS
    open: panel.groupListOpen
    // Shown on hover, it leaves the keyboard where it is.
    initialFocus: null
    implicitWidth: 280
    implicitHeight: 2 * padding + groupWindows.count * rowHeight + Math.max(0, groupWindows.count - 1) * rows.spacing
    anchorRect: panel.dockAnchor(panel.groupX, 0)
    side: panel.dockSide
    HoverHandler {
        id: groupHover
        onHoveredChanged: groupList.panel.hoverGroupList(hovered)
    }
    readonly property bool hovered: groupHover.hovered
    // The row of the window at `index`, for the keyboard on the bar (the panel's barKeys).
    function windowAt(index) { return windowRows.itemAt(index) }
    TaskFilter {
        id: groupWindows
        controller: shell; sourceModel: panel.taskSource
        app: panel.groupSlot; windowApp: panel.groupWindowApp
        // A window closing may leave nothing to choose between.
        onCountChanged: if (count < 2 && groupList.open) panel.groupOpen = false
    }
    Column {
        id: rows
        anchors.fill: parent; anchors.margins: groupList.padding; spacing: Theme.spacingXS
        Repeater {
            id: windowRows
            model: groupList.visible ? groupWindows : null
            delegate: Button {
                id: groupWindow
                required property int index
                required property int taskId
                required property string title
                required property string appId
                required property bool active
                required property bool minimized
                required property bool urgent
                objectName: "groupWindow"
                width: parent.width; height: groupList.rowHeight
                Accessible.name: title
                // Closing the list destroys this row, so it goes last.
                onClicked: { shell.tasks.activate(taskId); panel.groupOpen = false }
                // The keyboard on the bar is at it, which rings it and shows its cross for Delete;
                // the Menu key there opens its window's menu.
                readonly property bool keySelected: groupList.panel.barKeys.active &&
                                                    groupList.panel.barKeys.window === index
                function keyMenu() { groupList.panel.openContextMenu(groupWindow, 0, taskId, appId) }
                background: ButtonFill {
                    // Lit under a drag too, which brings its window forward (the panel's dragDelay).
                    hovered: groupWindow.hovered || groupList.panel.dragTile === groupWindow
                    pressed: groupWindow.pressed
                    active: groupWindow.active
                    FocusRing {
                        objectName: "groupWindowFocusRing"
                        visible: groupWindow.keySelected
                        radius: parent.radius
                    }
                    Rectangle {
                        objectName: "groupWindowLine"
                        visible: groupWindow.active || groupWindow.urgent
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3; height: Theme.iconSizeSmall; radius: 1
                        color: groupWindow.urgent ? Theme.urgent : Theme.accent
                    }
                }
                contentItem: RowLayout {
                    spacing: Theme.spacingM
                    Image {
                        Layout.leftMargin: Theme.spacingS
                        Layout.preferredWidth: Theme.appIconSize; Layout.preferredHeight: Theme.appIconSize
                        source: "image://icons/" + panel.groupIcon; sourceSize: Qt.size(Theme.appIconSize, Theme.appIconSize)
                        opacity: groupWindow.minimized ? 0.5 : 1
                    }
                    Text {
                        Layout.fillWidth: true
                        text: groupWindow.title; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: groupWindow.urgent ? Theme.urgent : groupWindow.minimized ? Theme.textMuted : Theme.text
                        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                    }
                    CloseButton {
                        objectName: "groupWindowClose"
                        visible: groupWindow.hovered || hovered || groupWindow.keySelected
                        danger: true
                        Layout.preferredWidth: size; Layout.preferredHeight: size
                        Accessible.name: "Close " + groupWindow.title
                        onClicked: shell.tasks.close(groupWindow.taskId)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            panel.openContextMenu(groupWindow, 0, groupWindow.taskId, groupWindow.appId)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) shell.tasks.close(groupWindow.taskId)
                    }
                }
            }
        }
    }
}
