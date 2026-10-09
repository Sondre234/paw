// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Across the top of every output while the configuration has an error: what is wrong, until a
// save or reload loads it. Its colours are fixed, since the configured ones are not in use.
Rectangle {
    id: banner
    readonly property string message: shell.configError
    readonly property bool visibleNow: message.length > 0
    implicitHeight: column.implicitHeight + 16
    color: "#b3263a"

    Column {
        id: column
        x: 14; y: 8
        width: parent.width - 28
        spacing: 3
        Text {
            objectName: "configErrorTitle"
            width: parent.width
            text: "Configuration error: using the default configuration until it is fixed"
            color: "#ffffff"
            font.bold: true
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Text {
            objectName: "configErrorMessage"
            width: parent.width
            text: banner.message
            color: "#ffe9ec"
            font.family: "monospace"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 6
            elide: Text.ElideRight
        }
    }
}
