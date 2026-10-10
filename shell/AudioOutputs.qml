// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The volume control's other popup: the outputs to play through, the one in use marked.
PopupMenu {
    id: audioOutputs
    required property var panel
    parent: panel.popupLayer
    objectName: "audioOutputs"
    entryName: "audioOutputItem"
    open: panel.audioPopup === "outputs"
    // Centred below its item, or from its left edge as macOS's menus open.
    anchorRect: panel.barAnchor(panel.audioPopupX - panel.audioPopupWidth / 2, panel.audioPopupWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 240
    onDismissed: panel.audioPopup = ""
    entries: [{ header: "Output" }].concat(panel.audioSource.outputs.map(function(output) {
        return { text: output.description, toggle: "radio", checked: output.name === audioOutputs.panel.audioSource.output,
                 run: function() { audioOutputs.panel.audioSource.setOutput(output.name) } }
    }))
}
