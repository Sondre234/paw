// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The start menu, after Windows 11's: a search field on top, the applications under it, and
// along the bottom who is logged in and the power button, in the bottom-right corner, with its
// menu. The applications are read again as they are installed and removed.
PopupCard {
    id: launcher
    required property var panel
    parent: panel.popupLayer
    objectName: "launcher"
    open: panel.launcherOpen
    // The keyboard goes to the power menu while it is open, else to the search field.
    initialFocus: null
    onOpened: {
        search.text = ""
        allApps = false
        now = new Date()
        home.reset()
        allView.reset()
        menuApp = null
        // Once it is in its place: the first time, it opens while its loader is still making it,
        // and the keyboard would stay with the loader it then leaves.
        Qt.callLater(takeFocus)
    }
    function takeFocus() {
        if (panel.powerOpen) powerMenu.forceActiveFocus()
        else search.forceActiveFocus()
    }
    Connections {
        target: launcher.panel
        function onPowerOpenChanged() { if (launcher.open) launcher.takeFocus() }
    }
    // By the bar's start, 640 by 720 pixels, or as tall as the output leaves room for.
    implicitWidth: 640
    implicitHeight: 720
    // By the start button at the bar's start, or above the dock's applications button, its first.
    anchorRect: panel.macos ? panel.dockAnchor(panel.bar.x + Theme.dockPadding, 0)
                            : panel.barAnchor(Theme.spacingL + shell.panelMarginLeft, 0)
    alignment: Qt.AlignLeft
    side: panel.dockSide
    // Between the menu bar and the dock in the macOS style.
    bounds: panel.macos ? panel.popupArea : Qt.rect(0, 0, parent.width, parent.height)
    radius: Theme.radiusLarge
    // The space between the card's edges and what is on it.
    readonly property real padding: Theme.spacingXXL + Theme.spacingL
    // What it shows: the pinned and recent applications ("home"), all of them ("all"), or what
    // the search finds ("search") while there is text to search for.
    property bool allApps: false
    readonly property string view: search.text.trim() !== "" ? "search" : allApps ? "all" : "home"
    // The time the recent applications' "5 min ago" is told from.
    property date now: new Date()
    Timer { interval: 30000; repeat: true; running: launcher.open; onTriggered: launcher.now = new Date() }
    // The views change with a fade and a slide of this far, once the menu has opened.
    readonly property bool settled: open && progress === 1
    readonly property real slide: 2 * Theme.spacingXXL
    // The menu of an application here: its record (null while it is closed), whether it is of a
    // pinned tile, which can move to the front, and where it opens, in the popups' coordinates.
    property var menuApp: null
    property bool menuTile: false
    property bool menuByKeyboard: false
    property rect menuAnchor
    // The keyboard comes back from an application's menu as it closes.
    onMenuAppChanged: if (menuApp === null && open) takeFocus()

    function launch(id) {
        if (shell.launch(id))
            panel.closeMenus()
    }
    function launchAction(id, action) {
        if (shell.launchAction(id, action))
            panel.closeMenus()
    }
    // Runs a result of the search: launches an application, or does what the palette does with
    // a window, workspace or action. The menu closes first, handing the keyboard back.
    function run(result) {
        if (result.kind === "app") {
            launch(result.appId)
        } else {
            var output = panel.outputName
            panel.closeMenus()
            shell.palette.run(result, output)
        }
    }
    // Opens the folder of a file the search found, the menu closing first.
    function openFolder(result) {
        panel.closeMenus()
        shell.palette.openFolder(result)
    }
    // The keys the search field leaves: what moves through the view shown (the arrows, Tab, the
    // page keys), Enter, which runs what the keyboard is at (in the search, the best match's
    // button it is at), and Escape, which closes the letters of All apps, else clears the
    // search, else closes the menu.
    function key(event) {
        var shown = view === "search" ? searchView : view === "all" ? allView : home
        if (event.key === Qt.Key_Escape) {
            if (shown === allView && allView.lettersOpen)
                allView.lettersOpen = false
            else if (search.text !== "")
                search.text = ""
            else
                panel.closeMenus()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (shown === searchView)
                searchView.activate((event.modifiers & Qt.ControlModifier) !== 0)
            else if (shown.currentApp)
                launch(shown.currentApp.appId)
            event.accepted = true
        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            var item = shown.currentItem()
            if (shown.currentApp && item)
                openAppMenu(shown.currentApp, item, 0, item.height, shown === home && home.current < home.pins.length, true)
            event.accepted = true
        } else {
            // Tab stays in the menu, whatever there is to move through.
            event.accepted = shown.key(event, search.cursorPosition === search.length) ||
                             event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
        }
    }
    // Opens it at (x, y) in `item`, as a right press there does, or by `item`'s bottom-left
    // corner with its first entry highlighted, for the keyboard.
    function openAppMenu(app, item, x, y, tile, byKeyboard) {
        var at = item.mapToItem(panel.popupLayer, x, y)
        menuAnchor = Qt.rect(at.x, at.y, 0, 0)
        menuTile = tile
        menuByKeyboard = !!byKeyboard
        menuApp = app
    }
    // Open, what its desktop entry offers besides (New Window, ...), then pinning it to the start
    // menu or the taskbar, or unpinning it, and a pinned tile's Move to front. A configured
    // launcher is only opened: the configuration pins it to the taskbar.
    function appMenuEntries(app, tile) {
        if (!app)
            return []
        var id = app.appId
        var entries = [{ text: "Open", icon: "app-window", objectName: "startMenu:open",
                         run: function() { launcher.launch(id) } }]
        if (app.configured)
            return entries
        entries = entries.concat(shell.appActions(id).map(function(action) {
            return { text: action.name, icon: action.icon, objectName: "startMenu:action:" + action.action,
                     run: function() { launcher.launchAction(id, action.action) } }
        }))
        // Reading the pins makes the entries follow them. Pinning waits until the click is
        // handled: the change rebuilds the menu, destroying the clicked row.
        var pins = shell.startMenu.pinned
        var pinned = pins.some(function(pin) { return pin.appId === id })
        entries.push({ separator: true })
        entries.push(pinned
            ? { text: "Unpin from Start", icon: "pin-off", objectName: "startMenu:unpin",
                run: function() { Qt.callLater(function() { shell.startMenu.unpin(id) }) } }
            : { text: "Pin to Start", icon: "pin", objectName: "startMenu:pin",
                run: function() { Qt.callLater(function() { shell.startMenu.pin(id) }) } })
        if (tile && pinned && pins[0].appId !== id) {
            var first = pins[0].appId
            entries.push({ text: "Move to front", icon: "arrow-up-to-line", objectName: "startMenu:front",
                           run: function() { Qt.callLater(function() { shell.startMenu.movePin(id, first) }) } })
        }
        var taskbar = panel.pinAction(id)
        var onTaskbar = shell.pinned.some(function(pin) { return pin.appId === id })
        taskbar.icon = onTaskbar ? "pin-off" : "pin"
        taskbar.objectName = "startMenu:taskbar"
        entries.push(taskbar)
        return entries
    }
    // For a preview (Panel.previewPopup): shows "launcher-all", every application,
    // "launcher-search", what a search finds, "launcher-empty", a search finding nothing, or
    // "launcher-menu", a pinned tile's menu.
    function preview(name) {
        if (name === "launcher-all") {
            allApps = true
        } else if (name === "launcher-search") {
            search.text = "fi"
        } else if (name === "launcher-empty") {
            search.text = "> zqxw" // with a prefix, as a word alone finds the web search
        } else if (name === "launcher-menu") {
            // The fourth tile's, which can move to the front, once the tiles are laid out.
            home.current = Math.min(3, home.pins.length - 1)
            if (!home.currentApp)
                return false
            previewMenu.start()
        } else {
            return false
        }
        return true
    }
    Timer {
        id: previewMenu
        interval: 50
        onTriggered: {
            var tile = home.currentItem()
            launcher.openAppMenu(home.currentApp, tile, tile.width / 2, tile.height / 2, true)
        }
    }

    SearchInput {
        id: search
        objectName: "applicationSearch"
        x: launcher.padding; y: launcher.padding
        width: launcher.width - 2 * launcher.padding
        rightPadding: clear.visible ? clear.width + Theme.spacingS : Theme.spacingL
        placeholderText: "Search apps, windows and actions"
        Keys.onPressed: (event) => launcher.key(event)
        // Clears the search, the keyboard staying in the field.
        CloseButton {
            id: clear
            objectName: "startClear"
            visible: search.text !== ""
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
            anchors.verticalCenter: parent.verticalCenter
            size: parent.height - 2 * Theme.spacingS
            Accessible.name: "Clear the search"
            onClicked: search.text = ""
        }
    }
    // A view, shown or not: it fades in sliding from `away` (a horizontal and a vertical
    // distance), and out back there. Changes are instant while the menu opens, which resets them.
    component View: Item {
        id: shownView
        property bool shown: false
        property point away: Qt.point(0, 0)
        property bool animated: false
        anchors.fill: parent
        opacity: shown ? 1 : 0
        visible: opacity > 0
        enabled: shown
        property real offsetX: shown ? 0 : away.x
        property real offsetY: shown ? 0 : away.y
        transform: Translate { x: shownView.offsetX; y: shownView.offsetY }
        Behavior on opacity {
            enabled: shownView.animated
            NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
        }
        Behavior on offsetX {
            enabled: shownView.animated
            NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
        }
        Behavior on offsetY {
            enabled: shownView.animated
            NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
        }
    }
    // The views, between the search field and the footer, across the card's whole width: All
    // apps comes in from the right as the pinned applications leave to the left, and what the
    // search finds rises over either.
    Item {
        id: views
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: search.bottom; anchors.bottom: footer.top
        anchors.topMargin: Theme.spacingL; anchors.bottomMargin: Theme.spacingM
        clip: true
        View {
            shown: launcher.view === "home"
            away: Qt.point(launcher.view === "all" ? -launcher.slide : 0, 0)
            animated: launcher.settled
            StartHome {
                id: home
                anchors.fill: parent
                launcher: launcher
                inset: launcher.padding - Theme.spacingM
            }
        }
        View {
            shown: launcher.view === "all"
            away: Qt.point(launcher.view === "home" ? launcher.slide : 0, 0)
            animated: launcher.settled
            StartAllApps {
                id: allView
                anchors.fill: parent
                launcher: launcher
                inset: launcher.padding - Theme.spacingM
            }
        }
        View {
            shown: launcher.view === "search"
            away: Qt.point(0, launcher.slide / 2)
            animated: launcher.settled
            StartSearch {
                id: searchView
                anchors.fill: parent
                launcher: launcher
                inset: launcher.padding - Theme.spacingM
                query: search.text.trim()
            }
        }
    }
    // Along the bottom, in a shade of its own: who is logged in, and the power button.
    Rectangle {
        id: footer
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        // Inside the card's outline.
        anchors.margins: 1
        height: 2 * Theme.rowHeight - Theme.spacingM
        color: Theme.surfaceRaised
        bottomLeftRadius: launcher.radius - 1; bottomRightRadius: launcher.radius - 1
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        UserAvatar {
            id: avatar
            x: launcher.padding; anchors.verticalCenter: parent.verticalCenter
            size: Theme.appIconSizeLarge
            backdrop: footer.color
        }
        Text {
            objectName: "userName"
            anchors.left: avatar.right; anchors.leftMargin: Theme.spacingL
            anchors.right: powerButton.left; anchors.rightMargin: Theme.spacingL
            anchors.verticalCenter: parent.verticalCenter
            text: shell.startMenu.userName; textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
        }
        // Lock, suspend and the rest, as far as the compositor says they may run.
        FlatButton {
            id: powerButton
            objectName: "powerButton"
            visible: shell.widgets.power && shell.power.available.length > 0
            anchors.right: parent.right; anchors.rightMargin: launcher.padding - Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.rowHeight; height: Theme.rowHeight
            active: launcher.panel.powerOpen
            onClicked: panel.powerOpen = !panel.powerOpen
            Accessible.name: "Power"
            BarTip {
                panel: launcher.panel; owner: powerButton
                visible: powerButton.hovered && !launcher.panel.powerOpen
                text: "Lock, suspend, power off"
            }
            contentItem: Item {
                Icon { anchors.centerIn: parent; name: "power"; color: panel.powerOpen ? Theme.accent : Theme.text }
            }
        }
    }
    // While the power menu or an application's is open, a press anywhere else in the launcher
    // closes it, and only it.
    MouseArea {
        anchors.fill: parent
        z: 1
        visible: panel.powerOpen || launcher.menuApp !== null
        acceptedButtons: Qt.AllButtons
        onPressed: { panel.powerOpen = false; launcher.menuApp = null }
    }
    // An application's menu, where it was asked for, over the launcher.
    PopupMenu {
        id: appMenu
        objectName: "startAppMenu"
        entryName: "startMenuEntry"
        parent: launcher.panel.popupLayer
        z: 1
        open: launcher.open && launcher.menuApp !== null
        entries: launcher.appMenuEntries(launcher.menuApp, launcher.menuTile)
        anchorRect: launcher.menuAnchor
        side: Qt.BottomEdge
        alignment: Qt.AlignLeft
        gap: 0
        bounds: launcher.panel.popupArea
        initialIndex: launcher.menuByKeyboard ? 0 : -1
        onDismissed: launcher.menuApp = null
    }
    // The power menu, above the power button and ending where it ends, over the launcher.
    PowerMenu {
        id: powerMenu
        panel: launcher.panel
        parent: launcher.panel.popupLayer
        z: 1
        anchorRect: Qt.rect(launcher.x + footer.x + powerButton.x, launcher.y + footer.y + powerButton.y,
                            powerButton.width, powerButton.height)
        side: Qt.TopEdge
        alignment: Qt.AlignRight
        gap: Theme.spacingS
        bounds: launcher.panel.popupArea
    }
}
