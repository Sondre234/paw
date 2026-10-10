// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Paw

// The menu bar of the macOS style, across the top of the output in a surface of its own
// (MenuBarWindow in view.hpp). At its left the system menu behind a spiral, the focused
// application's name in bold with its menu, and the Window menu (MenuBarMenu.qml); at its right
// the widgets shell.widgets puts on the bar, in the taskbar's order, then search (the command
// palette), Quick Settings and the clock, which opens the notifications and the calendar. Its
// menus and popups are the panel's, in its popover, below what opens them. While one of its
// menus is open, the pointer moving onto another's title opens that one instead.
Rectangle {
    id: menuBar
    required property var panel
    objectName: "menuBar"
    color: Theme.bar
    // The buttons the panel opens popups by, as Taskbar.qml names its own.
    readonly property Item systemButton: systemTitle
    readonly property Item appButton: appTitle
    readonly property Item windowButton: windowTitle
    readonly property Item trayRow: tray
    readonly property Item clock: clockButton
    readonly property Item volume: audioWidget
    readonly property Item network: networkWidget
    readonly property Item profiles: profilesButton
    readonly property Item wallpapers: wallpapersButton
    readonly property Item quickSettings: quickButton
    readonly property Item search: searchButton

    // The window the application's name and the menus are about: the focused one, which is kept
    // while a popup or the palette holds the keyboard (the compositor then tells of no window
    // focused) and a moment after, until the window has it back.
    property int focusedTask: -1
    readonly property bool holding: panel.menuOpen || shell.palette.output !== ""
    readonly property int activeTask: panel.allWindows.activeTask
    TaskFilter { id: focused; sourceModel: menuBar.panel.taskSource; taskId: menuBar.focusedTask }
    readonly property var focusedWindow: focusedTask >= 0 && focused.count > 0 ? focused.windows[0] : null
    function syncFocus() {
        if (activeTask >= 0 || (!holding && !focusSync.running))
            focusedTask = activeTask
    }
    onActiveTaskChanged: syncFocus()
    onHoldingChanged: if (!holding) focusSync.restart()
    Component.onCompleted: focusedTask = activeTask
    Timer { id: focusSync; interval: 400; onTriggered: menuBar.syncFocus() }
    // Its application, as shell.apps has it (null for one without an entry), and its name: the
    // entry's, else the app id's last part capitalised, else the window's title; "Desktop" while
    // no window is focused.
    readonly property var appRecord: focusedWindow ? panel.appRecord(focusedWindow.appId) : null
    readonly property string appName: {
        if (!focusedWindow)
            return "Desktop"
        if (appRecord)
            return appRecord.name
        var id = focusedWindow.appId.split(".").pop()
        return id ? id.charAt(0).toUpperCase() + id.slice(1) : focusedWindow.title
    }

    // A press on the bar beside its items closes what is open.
    MouseArea {
        anchors.fill: parent
        visible: menuBar.panel.menuOpen
        onPressed: menuBar.panel.closeMenus()
    }

    // A menu's title on the bar: a line icon or text, highlighted while its menu is open. It opens
    // on press, as macOS's do.
    component MenuTitle: Button {
        id: title
        required property var panel
        required property string kind
        property string glyph: ""
        property bool bold: false
        readonly property bool open: panel.menuBarMenu === kind
        objectName: kind + "MenuTitle"
        Layout.preferredHeight: Theme.barButtonHeight
        Layout.maximumWidth: 320
        leftPadding: Theme.menuBarPadding; rightPadding: Theme.menuBarPadding
        topPadding: 0; bottomPadding: 0
        hoverEnabled: true
        onPressed: panel.toggleMenuBarMenu(kind, title)
        onHoveredChanged: if (hovered) panel.hoverMenuBarMenu(kind, title)
        background: Rectangle {
            radius: Theme.menuBarRadius
            color: title.open || title.pressed ? Theme.selected : Theme.alpha(Theme.selected, 0)
        }
        contentItem: Item {
            implicitWidth: title.glyph ? glyphIcon.width : label.implicitWidth
            implicitHeight: label.implicitHeight
            Icon {
                id: glyphIcon
                visible: title.glyph !== ""
                anchors.centerIn: parent
                name: title.glyph
                size: Theme.menuBarIconSize
            }
            Text {
                id: label
                visible: title.glyph === ""
                anchors.fill: parent
                text: title.text
                textFormat: Text.PlainText
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: Theme.text
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                font.weight: title.bold ? Font.Bold : Font.Normal
            }
        }
    }

    RowLayout {
        id: menus
        anchors.left: parent.left; anchors.leftMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, status.x - Theme.spacingXL - x)
        spacing: 0
        MenuTitle {
            id: systemTitle
            panel: menuBar.panel
            kind: "system"
            glyph: "shell"
            Accessible.name: "System"
        }
        MenuTitle {
            id: appTitle
            panel: menuBar.panel
            kind: "app"
            text: menuBar.appName
            bold: true
            Layout.fillWidth: true
        }
        MenuTitle {
            id: windowTitle
            panel: menuBar.panel
            kind: "window"
            text: "Window"
        }
    }

    RowLayout {
        id: status
        anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXS
        BindingMode { panel: menuBar.panel }
        WorkspaceIndicator { panel: menuBar.panel }
        WallpapersButton { id: wallpapersButton; panel: menuBar.panel }
        ProfilesButton { id: profilesButton; panel: menuBar.panel }
        TilingButton { panel: menuBar.panel }
        // The system tray: the status icons of applications, in the order they appeared.
        // Passive ones stay hidden, and so does the tray when none is left.
        Row {
            id: tray
            objectName: "tray"
            visible: shell.widgets.tray && shell.tray.shown > 0
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: shell.tray
                delegate: TrayButton { panel: menuBar.panel }
            }
        }
        NotificationBell { panel: menuBar.panel }
        NetworkWidget { id: networkWidget; panel: menuBar.panel }
        BatteryWidget { panel: menuBar.panel }
        VolumeButton { id: audioWidget; panel: menuBar.panel }
        KeyboardLayout { panel: menuBar.panel }
        // Search: the command palette on this output, or away again.
        FlatButton {
            id: searchButton
            objectName: "searchButton"
            Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
            radius: Theme.menuBarRadius
            readonly property bool paletteHere: shell.palette.output !== "" && shell.palette.output === menuBar.panel.outputName
            active: paletteHere
            onClicked: {
                menuBar.panel.closeMenus()
                if (paletteHere)
                    shell.palette.close()
                else
                    shell.palette.open(menuBar.panel.outputName)
            }
            Accessible.name: "Search"
            BarTip { panel: menuBar.panel; owner: searchButton; text: "Search" }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "search"; size: Theme.menuBarIconSize - 1 }
            }
        }
        // Quick Settings, whatever widgets it holds.
        FlatButton {
            id: quickButton
            objectName: "quickSettingsButton"
            Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
            radius: Theme.menuBarRadius
            active: menuBar.panel.audioPopup === "quick"
            onClicked: menuBar.panel.toggleAudioPopup("quick", quickButton)
            Accessible.name: "Quick settings"
            BarTip { panel: menuBar.panel; owner: quickButton; text: "Quick settings" }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "toggles"; size: Theme.menuBarIconSize }
            }
        }
        ClockButton { id: clockButton; panel: menuBar.panel }
    }

    // What failed (an application that did not start, ...), across the bar until dismissed, as
    // the taskbar shows it.
    Rectangle {
        objectName: "menuBarError"
        visible: shell.error.length > 0
        anchors.fill: parent; anchors.margins: Theme.spacingXS
        color: Theme.dangerSurface; radius: Theme.menuBarRadius
        Text {
            anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter
            anchors.margins: Theme.spacingL
            text: shell.error; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        }
        CloseButton {
            id: dismiss
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingXS
            anchors.verticalCenter: parent.verticalCenter
            size: parent.height - Theme.spacingXS
            Accessible.name: "Dismiss"
            onClicked: shell.clearError()
        }
    }
}
