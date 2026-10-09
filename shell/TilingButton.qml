// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// Tiling on this panel's monitor (shell.widgets.tiling = "bar"), which a click turns on or off.
FlatButton {
    id: tilingToggle
    required property var panel
    objectName: "tilingToggle"
    visible: shell.widgets.tiling === "bar"
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    enabled: shell.tilingAvailable
    opacity: enabled ? 1 : 0.4
    active: panel.tiling
    onClicked: { panel.closeMenus(); shell.toggleTiling(panel.outputName) }
    Accessible.name: panel.tiling ? "Tiling on" : "Tiling off"
    BarTip { panel: tilingToggle.panel; owner: tilingToggle; text: tilingToggle.panel.tiling ? "Tiling on: click for floating" : "Floating: click to tile" }
    // On: a split layout in the accent colour. Off: two overlapping windows.
    contentItem: Item {
        FadingIcon {
            objectName: "tilingIcon"
            anchors.centerIn: parent
            name: tilingToggle.panel.tiling ? "layout-panel-left" : "copy"
            color: tilingToggle.panel.tiling ? Theme.accent : Theme.text
        }
    }
}
