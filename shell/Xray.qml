// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The names of the exploded view's sheets (paw xray). The compositor draws the stack of sheets
// over a dark backdrop; this sits over it in the output's coordinates with a legend of every
// sheet, top first, in the column the compositor leaves left of the stack (`legend` wide, none
// when 0), and a caption under the stack naming the sheet under the pointer, or saying how to
// work the view. It takes no input: the compositor keeps the keyboard and the pointer while the
// view is open.
Item {
    id: xray
    required property size screenSize
    // The compositor's view, unless set (as a preview sets them).
    property var sheets: shell.xraySheets
    property rect area: shell.xrayArea
    property int legend: shell.xrayLegend
    property int hovered: shell.xrayHovered
    // What was shown, held as it was while it fades: the controller forgets the sheets as the
    // view closes, before this hears that it should go.
    property var shownSheets: []
    property rect shownArea
    property int shownLegend: 0
    function hold() {
        if (sheets.length === 0)
            return
        shownSheets = sheets
        shownArea = area
        shownLegend = legend
    }
    Component.onCompleted: hold()
    onSheetsChanged: hold()
    onAreaChanged: hold()
    readonly property var hoveredSheet: hovered >= 0 && hovered < shownSheets.length
                                        ? shownSheets[hovered] : null
    width: screenSize.width
    height: screenSize.height
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1, its opacity: it fades in as the sheets come apart.
    property bool shown: false
    property alias progress: reveal.progress
    opacity: progress
    Reveal { id: reveal; shown: xray.shown; enterDuration: Theme.durationSlow }

    // What each layer is called, by the compositor's name for it.
    function layerName(layer) {
        switch (layer) {
        case "background": return qsTr("Background")
        case "bottom": return qsTr("Bottom layer")
        case "windows": return qsTr("Windows")
        case "floating": return qsTr("Floating")
        case "above": return qsTr("Kept above")
        case "fullscreen": return qsTr("Fullscreen")
        case "peek": return qsTr("Peek")
        case "top": return qsTr("Top layer")
        case "fullscreen_cover": return qsTr("Over the panels")
        case "x11": return qsTr("X11 menus")
        case "overlay": return qsTr("Overlay")
        }
        return layer
    }
    function sheetText(sheet) {
        return sheet.detail.length > 0 ? sheet.name + " — " + sheet.detail : sheet.name
    }

    // The legend: every sheet from the top down, each by a bar in its colour, the one under the
    // pointer marked. On the backdrop, which is dark whatever the theme, it stands on a card of the
    // theme's surface.
    Rectangle {
        id: legendCard
        objectName: "xrayLegend"
        visible: xray.shownLegend > 0 && xray.shownSheets.length > 0
        x: xray.shownArea.x - xray.shownLegend
        width: xray.shownLegend - Theme.spacingXL
        height: Math.min(rows.implicitHeight + 2 * Theme.spacingL, xray.shownArea.height)
        y: xray.shownArea.y + (xray.shownArea.height - height) / 2
        radius: Theme.radiusLarge
        color: Theme.surface
        border.color: Theme.border
        clip: true
        Column {
            id: rows
            x: Theme.spacingS; y: Theme.spacingL
            width: parent.width - 2 * Theme.spacingS
            spacing: Theme.spacingXS
            Text {
                x: Theme.spacingM
                bottomPadding: Theme.spacingS
                text: qsTr("Layers, top first")
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
            Repeater {
                model: xray.shownSheets.length
                delegate: Item {
                    id: row
                    required property int index
                    // From the top down.
                    readonly property int sheet: xray.shownSheets.length - 1 - index
                    readonly property var entry: xray.shownSheets[sheet]
                    readonly property bool lit: sheet === xray.hovered
                    width: rows.width
                    height: rowName.y + rowName.height + Theme.spacingS
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSmall
                        color: row.lit ? Theme.selected : "transparent"
                    }
                    Rectangle {
                        id: bar
                        x: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        width: 4; height: parent.height - 2 * Theme.spacingS
                        radius: 2
                        color: row.entry ? row.entry.colour : "transparent"
                    }
                    Text {
                        id: rowLayer
                        anchors.left: bar.right; anchors.leftMargin: Theme.spacingM
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
                        y: Theme.spacingS
                        text: row.entry ? xray.layerName(row.entry.layer) : ""
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                    }
                    Text {
                        id: rowName
                        anchors.left: rowLayer.left; anchors.right: rowLayer.right
                        anchors.top: rowLayer.bottom
                        text: row.entry ? xray.sheetText(row.entry) : ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                        font.weight: row.lit ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
    }

    // Under the stack: the sheet under the pointer, by its layer and what it holds; else how to
    // work the view, each key or gesture, then what it does.
    Rectangle {
        id: caption
        objectName: "xrayCaption"
        visible: xray.shownSheets.length > 0
        x: xray.shownArea.x + (xray.shownArea.width - width) / 2
        y: xray.shownArea.y + xray.shownArea.height + Theme.spacingL
        width: Math.min(xray.shownArea.width,
                        (xray.hoveredSheet ? named.implicitWidth : hints.implicitWidth)
                        + 2 * Theme.spacingXL)
        height: Math.max(named.implicitHeight, hints.implicitHeight) + 2 * Theme.spacingS
        radius: height / 2
        color: Theme.surface
        border.color: Theme.border
        Row {
            id: named
            visible: xray.hoveredSheet !== null
            anchors.centerIn: parent
            spacing: Theme.spacingM
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10; height: 10; radius: 5
                color: xray.hoveredSheet ? xray.hoveredSheet.colour : "transparent"
            }
            Text {
                id: hoveredLayer
                text: xray.hoveredSheet ? xray.layerName(xray.hoveredSheet.layer) : ""
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
            }
            Text {
                width: Math.min(implicitWidth, xray.shownArea.width - 2 * Theme.spacingXL
                                - hoveredLayer.implicitWidth - 10 - 2 * named.spacing)
                text: xray.hoveredSheet ? xray.sheetText(xray.hoveredSheet) : ""
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
            }
        }
        Row {
            id: hints
            visible: !named.visible
            anchors.centerIn: parent
            spacing: Theme.spacingL
            Repeater {
                model: [{ "key": qsTr("Move"), "does": qsTr("turns the stack") },
                        { "key": qsTr("Scroll"), "does": qsTr("spreads it") },
                        { "key": qsTr("Click"), "does": qsTr("goes to a window") },
                        { "key": qsTr("Esc"), "does": qsTr("closes") }]
                delegate: Text {
                    required property var modelData
                    text: "<b>" + modelData.key + "</b> " + modelData.does
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }
}
