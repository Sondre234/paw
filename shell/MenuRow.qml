// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// One row of a PopupMenu, drawn from its entry (`modelData`, see PopupMenu.qml): a separator, a
// section header, the menu's title (a large icon, the name, and a line under it), or an entry with
// its check mark, radio dot or icon, its label, its secondary text and, for a submenu, a chevron. `highlighted` is the row the pointer or the keyboard is at,
// `expanded` the one whose submenu is open; a disabled row is greyed out and cannot be chosen, and
// a destructive one (`danger`) is in the danger colour.
AbstractButton {
    id: row
    required property var modelData
    required property int index
    property bool highlighted: false
    property bool expanded: false
    // Whether the menu's rows keep a column for icons and marks, so that labels line up.
    property bool iconColumn: false
    property real rowHeight: Theme.menuRowHeight
    property real titleHeight: rowHeight
    property real labelStart: Theme.menuLabelInset
    property real labelEnd: Theme.spacingM

    readonly property bool separator: modelData.separator === true
    readonly property bool header: !separator && (modelData.header || "") !== ""
    readonly property bool title: !separator && !header && (modelData.title || "") !== ""
    readonly property bool submenu: !!modelData.submenu
    readonly property string toggle: modelData.toggle === "checkmark" ? "check" : (modelData.toggle || "")
    readonly property bool marked: modelData.checked === true
    readonly property bool danger: modelData.danger === true
    // Whether it can be chosen: drawn from the entry, so that a menu fading out, which takes no
    // input, does not grey its rows.
    readonly property bool available: !separator && !header && !title && modelData.enabled !== false
    // Whether it is filled with the macOS style's accent, its text then white.
    readonly property bool lit: Theme.macos && available && (pressed || highlighted || expanded)
    readonly property color ink: !available ? Theme.textDisabled : lit ? Theme.menuHighlightText
                                 : danger ? Theme.danger : Theme.text
    readonly property color muted: lit ? Theme.alpha(Theme.menuHighlightText, 0.8)
                                   : available ? Theme.textMuted : Theme.textDisabled

    text: header ? modelData.header : title ? modelData.title : (modelData.text || "")
    enabled: available
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: separator ? 2 * Theme.spacingS + 1 : header ? Theme.menuHeadingHeight
                    : title ? titleHeight : rowHeight
    Accessible.role: separator ? Accessible.Separator : title ? Accessible.StaticText : Accessible.MenuItem
    Accessible.description: title ? (modelData.secondary || "") : ""
    Accessible.name: text
    Accessible.checkable: toggle !== ""
    Accessible.checked: marked

    background: Rectangle {
        radius: Theme.menuRowRadius
        color: !row.available ? "transparent"
               : row.pressed ? Theme.menuPressed
               : row.highlighted || row.expanded ? Theme.menuHighlight : "transparent"
        Rectangle {
            visible: row.separator
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingS; width: parent.width - 2 * Theme.spacingS; height: 1
            color: Theme.divider
        }
    }
    // An icon Icon knows, else a theme icon's name or an image's URL, as an image source.
    function imageSource(icon) {
        return icon === "" ? "" : icon.indexOf(":") >= 0 || icon.charAt(0) === "/" ? icon : "image://icons/" + icon
    }
    contentItem: Item {
        // The menu's title: its icon large, the name, and the secondary text under it, elided.
        RowLayout {
            anchors.fill: parent
            visible: row.title
            spacing: Theme.spacingL
            Item {
                Layout.leftMargin: row.labelStart
                Layout.preferredWidth: Theme.appIconSizeLarge; Layout.preferredHeight: Theme.appIconSizeLarge
                Icon {
                    id: titleGlyph
                    visible: known
                    anchors.centerIn: parent
                    name: row.title ? (row.modelData.icon || "") : ""
                    size: Theme.appIconSizeLarge; color: Theme.text
                }
                Image {
                    visible: !titleGlyph.known
                    anchors.fill: parent
                    source: row.title && !titleGlyph.known ? row.imageSource(row.modelData.icon || "") : ""
                    sourceSize: Qt.size(Theme.appIconSizeLarge, Theme.appIconSizeLarge)
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.rightMargin: row.labelEnd
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: row.text; textFormat: Text.PlainText; elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily; font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: row.title ? (row.modelData.secondary || "") : ""; textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
            }
        }
        RowLayout {
            anchors.fill: parent
            visible: !row.separator && !row.title
            spacing: Theme.menuIconGap
            // A check mark, a radio button's dot, or the entry's icon: a line icon Icon knows, else
            // a theme icon's name or an image's URL.
            Item {
                visible: row.iconColumn && !row.header
                Layout.leftMargin: row.labelStart
                Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                // macOS marks the chosen one of a group with a check mark too.
                Icon {
                    visible: (row.toggle === "check" || (Theme.macos && row.toggle === "radio")) && row.marked
                    anchors.centerIn: parent
                    name: "check"; size: Theme.iconSizeSmall; color: row.lit ? row.ink : Theme.menuMark
                }
                Rectangle {
                    visible: !Theme.macos && row.toggle === "radio" && row.marked
                    anchors.centerIn: parent
                    width: Theme.spacingM; height: Theme.spacingM; radius: Theme.spacingS
                    color: row.lit ? row.ink : Theme.menuMark
                }
                Icon {
                    id: glyph
                    visible: row.toggle === "" && known
                    anchors.centerIn: parent
                    name: row.toggle === "" ? (row.modelData.icon || "") : ""
                    size: Theme.iconSizeSmall; color: row.ink
                }
                Image {
                    readonly property string icon: row.toggle === "" && !glyph.known ? (row.modelData.icon || "") : ""
                    visible: icon !== ""
                    anchors.fill: parent
                    opacity: row.available ? 1 : 0.4
                    source: row.imageSource(icon)
                    sourceSize: Qt.size(Theme.iconSizeSmall, Theme.iconSizeSmall)
                }
            }
            Text {
                Layout.fillWidth: true
                Layout.leftMargin: row.iconColumn && !row.header ? 0 : row.labelStart
                text: row.text; textFormat: Text.PlainText; elide: Text.ElideRight
                color: row.header ? Theme.textMuted : row.ink
                font.pixelSize: row.header ? Theme.menuHeadingSize : Theme.fontSize
                font.weight: row.header && Theme.macos ? Font.DemiBold : Font.Normal
                font.family: Theme.fontFamily
            }
            Text {
                visible: text !== "" && !row.header
                Layout.rightMargin: row.submenu ? 0 : row.labelEnd
                text: row.modelData.secondary || ""; textFormat: Text.PlainText
                color: row.muted
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
            Icon {
                visible: row.submenu && !row.header
                Layout.rightMargin: row.labelEnd
                name: "chevron-right"; size: Theme.iconSizeSmall
                color: row.muted
            }
        }
    }
}
