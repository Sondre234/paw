// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// Launchpad, the launcher of the macOS style (Launcher.qml is the taskbar style's): every
// application over the whole output, on pages of a grid, with the wallpaper dimmed behind them, a
// search field at the top and a dot for each page along the bottom. It fades in as its grid settles
// from a little larger than its size.
//
// Typing searches, and the grid then shows what the search finds, the best match highlighted. The
// arrows move the highlight, onto the next or the previous page past its edge; Page Up and Page
// Down, the wheel, a swipe of the touchpad, dragging the grid or clicking a dot change pages; Enter
// launches what is highlighted; Escape clears the search, else closes. A click on an application
// launches it, a right click opens its menu (Open, its desktop actions, keeping it in the dock),
// and a click beside the applications closes Launchpad.
Item {
    id: launchpad
    required property var panel
    parent: panel.popupLayer
    anchors.fill: parent
    objectName: "launchpad"
    readonly property bool open: panel.launcherOpen
    // How far it is open, from 0 to 1: its opacity, and what is left of the grid's zoom.
    property alias progress: reveal.progress
    visible: open || progress > 0
    Reveal { id: reveal; shown: launchpad.open; enterDuration: Theme.durationSlow; exitDuration: Theme.durationNormal }
    onOpenChanged: {
        if (!open)
            return
        search.text = ""
        page = 0
        current = -1
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
        target: launchpad.panel
        function onPowerOpenChanged() { if (launchpad.open) launchpad.takeFocus() }
    }

    // What the grid shows: every application by name, or what the search finds, best first.
    readonly property string query: search.text.trim()
    readonly property var shown: {
        var apps = shell.startMenu.apps // searched again as applications come and go
        return query === "" ? apps : shell.startMenu.search(query, []).filter(function(result) {
            return result.kind === "app"
        })
    }
    // A new search starts on its first page with the best match highlighted, for Enter.
    onShownChanged: {
        page = 0
        current = query !== "" && shown.length > 0 ? 0 : -1
    }
    // How the grid is cut into pages; tests make them smaller.
    property int columns: Theme.launchpadColumns
    property int rows: Theme.launchpadRows
    readonly property int pageSize: columns * rows
    readonly property int pageCount: Math.max(1, Math.ceil(shown.length / pageSize))
    // The page shown, and the application the keyboard is at (an index into `shown`; -1 for none).
    property int page: 0
    property int current: -1
    readonly property var currentApp: current >= 0 && current < shown.length ? shown[current] : null

    // The grid's place: most of the output's width, between the search field and the dots at the
    // bottom; an application's cell, and its icon, as large as the cell leaves room for.
    readonly property real fieldY: 2 * Theme.spacingXXL
    readonly property real gridTop: fieldY + search.height + 2 * Theme.spacingXXL
    readonly property real dotsY: height - 2 * Theme.spacingXXL - Theme.spacingS
    readonly property real gridWidth: Math.round(width * 0.84)
    readonly property real gridHeight: Math.max(0, dotsY - gridTop - Theme.spacingXL)
    readonly property real cellWidth: Math.floor(gridWidth / columns)
    readonly property real cellHeight: Math.floor(gridHeight / rows)
    readonly property real labelHeight: 2 * labelFont.height
    readonly property real iconSize: Math.max(Theme.appIconSizeLarge,
                                              Math.min(Theme.launchpadIconSize,
                                                       cellHeight - labelHeight - 3 * Theme.spacingS,
                                                       Math.round(cellWidth * 0.6)))
    FontMetrics { id: labelFont; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }

    function launch(id) {
        if (shell.launch(id))
            panel.closeMenus()
    }
    function showPage(index) {
        var next = Math.max(0, Math.min(pageCount - 1, index))
        if (next === page)
            return
        // The highlight goes along, to the same place on the other page, or its last application.
        if (current >= 0)
            current = Math.min(shown.length - 1, current + (next - page) * pageSize)
        page = next
    }
    // Moves the highlight `columnStep` columns and `rowStep` rows: from nowhere to the page's first
    // application, and past a page's left or right edge onto the page beside it, in the same row.
    function move(columnStep, rowStep) {
        if (shown.length === 0)
            return
        if (current < 0) {
            current = Math.min(shown.length - 1, page * pageSize)
            return
        }
        var onPage = current % pageSize, base = current - onPage
        var column = onPage % columns + columnStep, row = Math.floor(onPage / columns) + rowStep
        if (row < 0 || row >= rows)
            return
        if (column < 0) {
            base -= pageSize
            column = columns - 1
        } else if (column >= columns) {
            base += pageSize
            column = 0
        }
        if (base < 0 || base >= shown.length)
            return
        var next = Math.min(shown.length - 1, base + row * columns + column)
        // Down to a row the last page does not fill stays where it is.
        if (rowStep > 0 && next < current + rowStep * columns && base === current - onPage)
            return
        current = next
        page = Math.floor(current / pageSize)
    }
    // The keys the search field leaves.
    function key(event) {
        switch (event.key) {
        case Qt.Key_Escape:
            if (search.text !== "") search.text = ""
            else panel.closeMenus()
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (currentApp) launch(currentApp.appId)
            break
        case Qt.Key_Left: move(-1, 0); break
        case Qt.Key_Right: move(1, 0); break
        case Qt.Key_Up: move(0, -1); break
        case Qt.Key_Down: move(0, 1); break
        case Qt.Key_PageUp: showPage(page - 1); break
        case Qt.Key_PageDown: showPage(page + 1); break
        case Qt.Key_Menu:
            openCurrentMenu()
            break
        default:
            if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                openCurrentMenu()
                break
            }
            // Tab stays in Launchpad.
            event.accepted = event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
            return
        }
        event.accepted = true
    }

    // An application's menu: its record (null while closed), and where it opens, in the popups'
    // coordinates.
    property var menuApp: null
    property bool menuByKeyboard: false
    property rect menuAnchor
    onMenuAppChanged: if (menuApp === null && open) takeFocus()
    // The cell of the application the keyboard is at, or null.
    function currentItem() {
        var page = pages.itemAt(Math.floor(current / pageSize))
        return page && currentApp ? page.cellAt(current % pageSize) : null
    }
    // The menu of the application the keyboard is at, by its icon, its first entry highlighted.
    function openCurrentMenu() {
        var cell = currentItem()
        if (cell)
            openAppMenu(currentApp, cell, cell.width / 2, cell.height / 2, true)
    }
    function openAppMenu(app, item, x, y, byKeyboard) {
        var at = item.mapToItem(panel.popupLayer, x, y)
        menuAnchor = Qt.rect(at.x, at.y, 0, 0)
        menuByKeyboard = !!byKeyboard
        menuApp = app
    }
    // Open and what its desktop entry offers besides (New Window, ...), then keeping it in the dock
    // or taking it out. A configured launcher is only opened: the configuration pins it.
    function appMenuEntries(app) {
        if (!app)
            return []
        var id = app.appId
        var entries = [{ text: "Open", icon: "app-window", objectName: "launchpad:open",
                         run: function() { launchpad.launch(id) } }]
        if (app.configured)
            return entries
        var actions = shell.appActions(id)
        if (actions.length > 0)
            entries.push({ separator: true })
        entries = entries.concat(actions.map(function(action) {
            return { text: action.name, icon: action.icon, objectName: "launchpad:action:" + action.action,
                     run: function() {
                         if (shell.launchAction(id, action.action))
                             launchpad.panel.closeMenus()
                     } }
        }))
        // Reading the pins makes the entry follow them.
        var dock = panel.pinAction(id)
        var kept = shell.pinned.some(function(pin) { return pin.appId === id })
        dock.text = kept ? "Remove from Dock" : "Keep in Dock"
        dock.objectName = "launchpad:dock"
        entries.push({ separator: true })
        entries.push(dock)
        return entries
    }
    // For a preview (Panel.previewPopup), as the start menu's views are named: "launcher-all", the
    // keyboard at an application on the last page, "launcher-search", what a search finds,
    // "launcher-empty", a search finding nothing, or "launcher-menu", an application's menu.
    function preview(name) {
        if (name === "launcher-all") {
            current = shown.length - 1
            page = pageCount - 1
        } else if (name === "launcher-search") {
            search.text = "fi"
        } else if (name === "launcher-empty") {
            search.text = "zqxw"
        } else if (name === "launcher-menu") {
            current = Math.min(3, shown.length - 1)
            if (!currentApp)
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
            var cell = launchpad.currentItem()
            launchpad.openAppMenu(launchpad.currentApp, cell, cell.width / 2, cell.height / 2, false)
        }
    }

    // The wallpaper, as the desktop shows it, under a scrim. A click on it closes Launchpad, and
    // dragging it sideways pages, once it has gone far enough.
    Item {
        id: backdrop
        anchors.fill: parent
        opacity: launchpad.progress
        Loader {
            anchors.fill: parent
            active: shell.wallpaper.toString().length === 0
            sourceComponent: DrawnWallpaper {}
        }
        Image {
            anchors.fill: parent
            source: shell.wallpaper
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Rectangle {
            anchors.fill: parent
            color: Theme.launchpadScrim
        }
        MouseArea {
            id: drag
            anchors.fill: parent
            enabled: launchpad.open
            acceptedButtons: Qt.AllButtons
            property real from: 0
            property bool active: false
            // How far the grid follows the pointer.
            readonly property real moved: active ? mouseX - from : 0
            onPressed: (mouse) => {
                from = mouse.x
                active = false
            }
            onPositionChanged: (mouse) => {
                if ((pressedButtons & Qt.LeftButton) && Math.abs(mouse.x - from) > Qt.styleHints.startDragDistance)
                    active = true
            }
            onReleased: (mouse) => {
                if (active) {
                    var moved = mouse.x - from
                    active = false
                    if (Math.abs(moved) > launchpad.width / 8)
                        launchpad.showPage(launchpad.page + (moved < 0 ? 1 : -1))
                } else if (mouse.button === Qt.LeftButton && containsMouse) {
                    launchpad.panel.closeMenus()
                }
            }
        }
    }

    Item {
        id: content
        anchors.fill: parent
        enabled: launchpad.open
        opacity: launchpad.progress
        scale: 1.08 - 0.08 * launchpad.progress

        // A small rounded field, its magnifier and "Search" centred while it is empty.
        TextField {
            id: search
            objectName: "launchpadSearch"
            readonly property real glyph: Theme.iconSizeSmall
            readonly property real empty: (width - glyph - Theme.spacingS - hint.advanceWidth(placeholderText)) / 2
            x: Math.round((launchpad.width - width) / 2); y: launchpad.fieldY
            implicitWidth: Theme.launchpadFieldWidth; implicitHeight: Theme.launchpadFieldHeight
            leftPadding: (text === "" ? empty : Theme.spacingM) + glyph + Theme.spacingS
            rightPadding: Theme.spacingM
            topPadding: 0; bottomPadding: 0
            verticalAlignment: TextInput.AlignVCenter
            placeholderText: "Search"
            color: Theme.launchpadText
            placeholderTextColor: Theme.alpha(Theme.launchpadText, 0.65)
            selectionColor: Theme.accent
            selectedTextColor: Theme.textOnAccentFill
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
            Keys.onPressed: (event) => launchpad.key(event)
            FontMetrics { id: hint; font: search.font }
            background: Rectangle {
                radius: Theme.radiusSmall + 1
                color: Theme.launchpadField
                border.color: Theme.alpha(Theme.launchpadText, 0.18)
                Icon {
                    x: search.leftPadding - width - Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"; size: search.glyph
                    color: Theme.alpha(Theme.launchpadText, 0.75)
                }
            }
        }

        // The pages, side by side, each as wide as the output; the one shown slides into place,
        // following a drag.
        Row {
            id: strip
            objectName: "launchpadPages"
            y: launchpad.gridTop
            x: -launchpad.page * launchpad.width + drag.moved
            Behavior on x {
                enabled: !drag.active
                NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
            }
            Repeater {
                id: pages
                model: launchpad.pageCount
                delegate: Item {
                    id: pageItem
                    required property int index
                    width: launchpad.width; height: launchpad.gridHeight
                    function cellAt(position) { return cells.itemAt(position) }
                    Item {
                        x: Math.round((parent.width - width) / 2)
                        width: launchpad.columns * launchpad.cellWidth; height: parent.height
                        Repeater {
                            id: cells
                            model: launchpad.shown.slice(pageItem.index * launchpad.pageSize,
                                                         (pageItem.index + 1) * launchpad.pageSize)
                            delegate: AbstractButton {
                                id: cell
                                required property var modelData
                                required property int index
                                readonly property int position: pageItem.index * launchpad.pageSize + index
                                objectName: "launchpadApp:" + modelData.appId
                                x: index % launchpad.columns * launchpad.cellWidth
                                y: Math.floor(index / launchpad.columns) * launchpad.cellHeight
                                width: launchpad.cellWidth; height: launchpad.cellHeight
                                focusPolicy: Qt.NoFocus
                                Accessible.name: modelData.name
                                onClicked: launchpad.launch(modelData.appId)
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton
                                    onPressed: (mouse) => launchpad.openAppMenu(cell.modelData, cell, mouse.x, mouse.y, false)
                                }
                                // The keyboard's: a rounded square behind the icon and its name.
                                background: Rectangle {
                                    x: Math.round((cell.width - width) / 2)
                                    y: icon.y - Theme.spacingM
                                    width: Math.min(cell.width, launchpad.iconSize + 3 * Theme.spacingL)
                                    height: launchpad.iconSize + launchpad.labelHeight + 2 * Theme.spacingM + Theme.spacingS
                                    radius: Theme.radiusLarge
                                    color: launchpad.current === cell.position ? Theme.launchpadHighlight : "transparent"
                                }
                                contentItem: Item {}
                                Image {
                                    id: icon
                                    x: Math.round((cell.width - width) / 2)
                                    y: Math.round((cell.height - height - launchpad.labelHeight - Theme.spacingS) / 2)
                                    width: launchpad.iconSize; height: width
                                    sourceSize: Qt.size(width, height)
                                    source: "image://icons/" + cell.modelData.icon
                                    asynchronous: true
                                    // Darkened while pressed, as macOS does.
                                    opacity: cell.pressed ? 0.7 : 1
                                }
                                Text {
                                    anchors.top: icon.bottom; anchors.topMargin: Theme.spacingS
                                    x: Theme.spacingS; width: cell.width - 2 * Theme.spacingS
                                    horizontalAlignment: Text.AlignHCenter
                                    text: cell.modelData.name; textFormat: Text.PlainText
                                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                                    color: Theme.launchpadText
                                    style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
                                    font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                }
                            }
                        }
                    }
                }
            }
        }
        // A wheel notch, or a touchpad's swipe of as much, pages once: down or right to the next.
        WheelHandler {
            // Qt takes the whole pointer for a touchpad once the compositor offers gestures
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            property real travel: 0
            onWheel: (event) => {
                var delta = Math.abs(event.angleDelta.x) > Math.abs(event.angleDelta.y) ? event.angleDelta.x
                                                                                       : event.angleDelta.y
                travel += delta
                var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                travel -= steps * 120
                if (steps !== 0)
                    launchpad.showPage(launchpad.page - steps)
            }
        }

        // What a search that finds nothing says.
        Text {
            objectName: "launchpadNothing"
            visible: launchpad.query !== "" && launchpad.shown.length === 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: launchpad.gridTop + launchpad.gridHeight / 3
            text: "No Results"
            color: Theme.alpha(Theme.launchpadText, 0.7)
            style: Text.Raised; styleColor: Qt.rgba(0, 0, 0, 0.35)
            font.pixelSize: Theme.fontSizeTitle + Theme.spacingS; font.family: Theme.fontFamily
        }

        // A dot for each page, the one shown brighter; clicking one shows its page.
        Row {
            id: dots
            objectName: "launchpadDots"
            visible: launchpad.pageCount > 1
            anchors.horizontalCenter: parent.horizontalCenter
            y: launchpad.dotsY
            Repeater {
                model: launchpad.pageCount
                delegate: AbstractButton {
                    id: dot
                    required property int index
                    objectName: "launchpadDot:" + index
                    width: Theme.launchpadDot + 2 * Theme.spacingS + Theme.spacingXS; height: width
                    focusPolicy: Qt.NoFocus
                    Accessible.name: "Page " + (index + 1)
                    onClicked: launchpad.showPage(index)
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: Theme.launchpadDot; height: width; radius: width / 2
                        color: Theme.alpha(Theme.launchpadText, dot.index === launchpad.page ? 0.9 : 0.35)
                        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    }
                    contentItem: Item {}
                }
            }
        }
    }

    // While the power menu or an application's is open, a press anywhere else closes it, and only
    // it.
    MouseArea {
        anchors.fill: parent
        z: 1
        visible: panel.powerOpen || launchpad.menuApp !== null
        acceptedButtons: Qt.AllButtons
        onPressed: { panel.powerOpen = false; launchpad.menuApp = null }
    }
    PopupMenu {
        id: appMenu
        objectName: "launchpadMenu"
        entryName: "launchpadMenuEntry"
        parent: launchpad.panel.popupLayer
        z: 1
        open: launchpad.open && launchpad.menuApp !== null
        entries: launchpad.appMenuEntries(launchpad.menuApp)
        anchorRect: launchpad.menuAnchor
        side: Qt.BottomEdge
        alignment: Qt.AlignLeft
        gap: 0
        initialIndex: launchpad.menuByKeyboard ? 0 : -1
        onDismissed: launchpad.menuApp = null
    }
    // The power menu (the power_menu action), under the search field.
    PowerMenu {
        id: powerMenu
        panel: launchpad.panel
        parent: launchpad.panel.popupLayer
        z: 1
        anchorRect: Qt.rect(search.x, search.y, search.width, search.height)
        side: Qt.BottomEdge
        alignment: Qt.AlignHCenter
        gap: Theme.spacingS
    }
}
