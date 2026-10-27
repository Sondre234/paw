// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// Tiling on this panel's monitor (shell.widgets.tiling = "bar"), which a click turns on or off.
// A right click opens its menu (TilingMenu.qml): the layout in use, and the others to pick.
FlatButton {
    id: tilingToggle
    required property var panel
    objectName: "tilingToggle"
    visible: shell.widgets.tiling === "bar"
    Layout.preferredWidth: panel.barItemWidth; Layout.preferredHeight: panel.barItemHeight
    enabled: shell.tilingAvailable
    opacity: enabled ? 1 : 0.4
    active: panel.tiling
    onClicked: { panel.closeMenus(); shell.toggleTiling(panel.outputName) }
    Accessible.name: panel.tiling ? "Tiling on" : "Tiling off"
    BarTip { panel: tilingToggle.panel; owner: tilingToggle; text: (tilingToggle.panel.tiling ? "Tiling on: click for floating" : "Floating: click to tile") + "\nRight-click: layouts" }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: tilingToggle.panel.toggleAudioPopup("tiling", tilingToggle)
    }
    // The tiling layout of the workspace this monitor shows, as its menu names it.
    readonly property string layout: (shell.workspaces[panel.outputName] || {}).layout || ""
    // On: the layout's icon, as its menu draws it (a split screen until the layout is known), in
    // the accent colour. Off: two overlapping windows.
    contentItem: Item {
        FadingIcon {
            objectName: "tilingIcon"
            anchors.centerIn: parent
            bar: true
            size: Theme.barIconSize
            name: !tilingToggle.panel.tiling ? "copy"
                : tilingToggle.layout !== "" ? "tiling-" + tilingToggle.layout : "layout-panel-left"
            color: tilingToggle.panel.tiling ? Theme.accent : Theme.text
        }
    }
}
