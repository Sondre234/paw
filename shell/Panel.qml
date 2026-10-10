// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw

Item {
    id: root
    // Set by the view: itself, and the compositor's name for the output it is on.
    required property var shellView
    required property string outputName
    property bool launcherOpen: false
    // The power menu, over the launcher by the power button in its bottom-right corner.
    property bool powerOpen: false
    // The popups are made when first needed, or a moment after startup so that the first use
    // finds them ready: what the bar shows first does not wait for them.
    property bool warm: false
    Timer { interval: 1500; running: true; onTriggered: root.warm = true }
    // A popup's loader in the popover: it makes the popup at once while `wanted`, and in the
    // background with `early` (as the bar warms up); once made a popup stays, so closing it never
    // destroys the item its handler runs in.
    component PopupLoader: Loader {
        property bool wanted: false
        property bool early: false
        property bool used: false
        asynchronous: !wanted
        active: wanted || early || used
        onLoaded: used = true
    }
    onWarmChanged: if (warm) popover.prepare()
    // The context menu belongs to a task, a pinned application (pinMenuApp), or the bar itself
    // when barMenuOpen is set. A task's menu offers to pin the application it belongs to.
    property int taskMenuId: -1
    property string taskMenuApp: ""
    // A stacked button's menu is about all its windows: where its hover list finds them, as
    // {slot, windowApp}; null for one window's.
    property var taskMenuGroup: null
    property var pinMenuApp: null
    property bool barMenuOpen: false
    property real contextMenuX: 0
    // The windows the taskbar shows; a stand-in model replaces it in tests.
    property var taskSource: shell.tasks
    // The sound server, replaced in tests too. Its popup is "mixer" (a slider per application)
    // or "outputs" (the output to play through), shown above the volume control.
    property var audioSource: shell.audio
    // Battery and network state; tests swap in one that reads a fake sysfs.
    property var statusSource: shell.status
    // The screen backlight, swapped the same way.
    property var backlightSource: shell.backlight
    // The media players, swapped the same way.
    property var mediaSource: shell.media
    // The power mode, swapped the same way.
    property var powerModeSource: shell.powerMode
    // Wi-Fi and the connection out (NetworkManager), swapped the same way. Its popup on the bar is
    // "wifi", by the network widget.
    property var wifiSource: shell.wifi
    // Bluetooth (BlueZ), swapped the same way.
    property var bluetoothSource: shell.bluetooth
    property string audioPopup: ""
    // The middle of the item it opens by, and the item's width.
    property real audioPopupX: 0
    property real audioPopupWidth: 0
    // A tray item's menu: the item it belongs to ("" while closed), and where its icon is.
    property string trayMenuKey: ""
    property real trayMenuX: 0
    property real trayMenuWidth: 0
    // Bumped when the open menu's item changes its entries, so they are read again.
    property int trayMenuRevision: 0
    // The menu bar's own menus in the macOS style: "system", "app" or "window" while one is open,
    // below the item whose left edge is at menuBarMenuX.
    property string menuBarMenu: ""
    property real menuBarMenuX: 0
    property bool menuOpen: launcherOpen || taskMenuId >= 0 || pinMenuApp !== null || barMenuOpen || audioPopup !== "" ||
                            trayMenuKey !== "" || menuBarMenu !== ""
    // Hovering an application's stacked button lists its windows above it: those of the pinned
    // application groupSlot, or with the app id groupWindowApp outside the pinned slots, or the
    // window groupTask alone when it is 0 or more (a button standing for one window).
    property bool groupOpen: false
    property string groupSlot: ""
    property string groupWindowApp: ""
    property int groupTask: -1
    property string groupIcon: ""
    property real groupX: 0
    property Item groupPending: null
    readonly property TaskFilter groupWindows: TaskFilter {
        controller: shell; sourceModel: root.taskSource
        app: root.groupSlot; windowApp: root.groupWindowApp; taskId: root.groupTask
    }
    // In the taskbar style with shell.thumbnails, resting on any button with windows shows them
    // as pictures instead (WindowThumbnails.qml): a tile for each side by side, `thumbnailGap`
    // apart and inside the card, each holding its window's picture `thumbnailPadding` inside it,
    // thumbnailWidth wide: shell.thumbnailSize, or narrower so that the card fits across the
    // output (with PopupCard's margin), but no narrower than 60 % of it. More windows than fit so
    // are listed after all.
    readonly property bool thumbnails: shell.thumbnails && !macos
    // What shows a button's windows changes with it, and what was shown goes.
    onThumbnailsChanged: closeGroup()
    readonly property int thumbnailGap: Theme.spacingS
    readonly property int thumbnailPadding: Theme.spacingM
    readonly property real thumbnailWidth: thumbnailWidthFor(groupWindows.count)
    function thumbnailWidthFor(count) {
        count = Math.max(1, count)
        var room = popupArea.width - 2 * Theme.spacingM - (count + 1) * thumbnailGap
        return Math.min(shell.thumbnailSize, Math.floor(room / count) - 2 * thumbnailPadding)
    }
    // Whether `count` windows' pictures fit on the card, else their list shows.
    function picturesFit(count) { return count < 2 || thumbnailWidthFor(count) >= 0.6 * shell.thumbnailSize }
    readonly property bool thumbnailsOpen: groupOpen && thumbnails && picturesFit(groupWindows.count)
    // The pictures of windows are the task source's, taken while something here wants them
    // (wantPicture; a stand-in source has no watchPicture, and none are taken). The card's tiles
    // want their windows', and the panel those of the button the pointer has rested on for half
    // of shell.thumbnailDelay (warmGroup), so that its card opens on them rather than on the
    // icons standing in until they come: a pointer only crossing a button on its way elsewhere
    // has left it by then, and the other half leaves a picture time to come. Wants are counted by
    // task id and asked of the source once a change is over (syncPictures), so that as the card
    // opens its tiles take over the panel's wants without the windows being let go and asked for
    // again: their captures go on, and without shell.liveThumbnails no window's one picture is
    // taken twice.
    property var pictureWants: ({})
    // The windows asked for, as {taskId: true}, and the source they were asked of.
    property var pictureWatches: ({})
    property var pictureSource: null
    function wantPicture(taskId, wanted) {
        var count = (pictureWants[taskId] || 0) + (wanted ? 1 : -1)
        if (count > 0) pictureWants[taskId] = count
        else delete pictureWants[taskId]
        Qt.callLater(syncPictures)
    }
    function syncPictures() {
        var source = taskSource, id
        if (pictureSource !== source) {
            if (pictureSource && typeof pictureSource.unwatchPicture === "function")
                for (id in pictureWatches) pictureSource.unwatchPicture(Number(id))
            pictureWatches = {}
            pictureSource = source
        }
        for (id in pictureWatches)
            if (!pictureWants[id]) {
                source.unwatchPicture(Number(id))
                delete pictureWatches[id]
            }
        if (!source || typeof source.watchPicture !== "function")
            return
        for (id in pictureWants)
            if (!pictureWatches[id]) {
                source.watchPicture(Number(id), Math.round(shell.thumbnailSize * Screen.devicePixelRatio),
                                    shell.liveThumbnails)
                pictureWatches[id] = true
            }
    }
    // The windows of the button the pointer rests on, warmed up for its card, as windowsOf gives
    // them; null while none are.
    property var warmGroup: null
    readonly property TaskFilter warmWindows: TaskFilter {
        controller: shell; sourceModel: root.warmGroup ? root.taskSource : null
        app: root.warmGroup ? root.warmGroup.slot : ""
        windowApp: root.warmGroup ? root.warmGroup.windowApp : ""
        taskId: root.warmGroup ? root.warmGroup.task : -1
    }
    Instantiator {
        model: root.warmWindows
        delegate: QtObject {
            required property int taskId
            // The window it wanted, should the row change before it goes.
            property int wanted: -1
            Component.onCompleted: { wanted = taskId; root.wantPicture(wanted, true) }
            Component.onDestruction: root.wantPicture(wanted, false)
        }
    }
    function warmPictures() {
        var button = groupPending
        if (!groupShow.running || groupOpen || !button || !button.hovered || !thumbnails)
            return
        warmGroup = windowsOf(button)
        // Too many to show as pictures, they are listed instead.
        if (!picturesFit(warmWindows.count))
            warmGroup = null
    }
    Timer { id: pictureWarmup; interval: shell.thumbnailDelay / 2; onTriggered: root.warmPictures() }
    readonly property bool groupListOpen: groupOpen && !thumbnailsOpen
    // What shows the windows: the card of pictures or the list, and whether the pointer is on it.
    readonly property Item groupPopup: thumbnailsOpen ? thumbnailsLoader.item : groupListLoader.item
    readonly property bool groupListHovered: groupPopup ? groupPopup.hovered : false
    // Something is open in the popover, or the bar has the keyboard there. The list shown on
    // hover does not take the keyboard: it opens under a window being typed in.
    readonly property bool expanded: menuOpen || groupOpen || barKeys.active
    onMenuOpenChanged: {
        if (menuOpen) {
            stopWaiting()
            groupOpen = false
            // One opened otherwise than from the keyboard on the bar takes the keyboard over.
            if (!barKeys.menuOpened)
                barKeys.leave()
        } else {
            barKeys.resume()
        }
    }
    // The keyboard on the bar (BarKeyboard.qml, the taskbar_focus action), which holds it in the
    // popover and walks the bar's buttons and the windows they show.
    readonly property Item barKeys: barKeyboard
    function toggleBarKeyboard() { barKeyboard.toggle() }
    // What the keyboard on the bar shows closing by itself (its last window gone, a menu opening
    // in its place) leaves the keyboard on its button.
    onGroupOpenChanged: if (!groupOpen && barKeys.active && !barKeys.menuOpened) barKeys.window = -1
    // The bars the style has: the taskbar (Taskbar.qml), or in the macOS style the dock
    // (Dock.qml) in the panel's surface and the menu bar (TopMenuBar.qml) in a surface of its own
    // along the output's top edge.
    readonly property bool macos: Theme.macos
    readonly property Item taskbar: taskbarLoader.item
    readonly property Item dock: dockLoader.item
    readonly property Item menuBar: menuBarLoader.item
    readonly property MenuBarWindow menuBarWindow: menuBarWindow
    // The bar along the panel's edge, which its popups open by; the panel itself while it is
    // being made.
    readonly property Item bar: taskbar ? taskbar.barItem : dock ? dock.barItem : root
    // The bar with the status widgets (the clock, Quick Settings, the tray, ...), whose popups
    // open by it, and the part that holds them and names the buttons they open by.
    readonly property Item statusBar: macos ? menuBar || root : bar
    readonly property Item statusArea: macos ? menuBar : taskbar
    // The surface the popups are drawn in, and the bar's top edge in its coordinates: the bar's
    // surface lies along its top or bottom edge, across its width, where it begins at surfaceTop.
    readonly property Item popupLayer: popupLayer
    // What Quick Settings opens the wallpaper picker by.
    readonly property Item quickSettingsButton: statusArea ? statusArea.quickSettings : null
    readonly property real surfaceTop: onTop ? 0 : popover.height - height
    readonly property real barTop: surfaceTop + bar.y
    // The menu bar's strip along the output's top edge, none without one.
    readonly property real menuBarHeight: macos ? Theme.menuBarHeight : 0
    // A popup of the bar opens away from the screen edge the bar is on (PopupCard's side), beside
    // the rectangle barAnchor gives: from `x`, `width` wide, and across the bar. In the macOS
    // style the bar with the widgets is the menu bar, and the dock's popups (an application's
    // menu, the windows of one) open above the dock by dockAnchor, which on the taskbar is the
    // bar's.
    readonly property int popupSide: onTop || macos ? Qt.BottomEdge : Qt.TopEdge
    function barAnchor(x, width) { return macos ? Qt.rect(x, 0, width, menuBarHeight) : dockAnchor(x, width) }
    readonly property int dockSide: macos ? Qt.TopEdge : popupSide
    function dockAnchor(x, width) { return Qt.rect(x, barTop, width, bar.height) }
    // The output but the bar's strip, where popups stay (PopupCard's bounds); between the menu
    // bar and the dock's strip in the macOS style.
    readonly property rect popupArea: macos
        ? Qt.rect(0, menuBarHeight, popover.width, popover.height - menuBarHeight - shell.panelExtent)
        : Qt.rect(0, onTop ? height : 0, popover.width, popover.height - height)
    // Where a list shown on hover takes the pointer: over it and down to the bar, so that the
    // pointer crossing from its button never lands on a window between (which, with focus
    // following the pointer, would take the keyboard).
    function hoverArea(item) {
        var top = onTop ? popupArea.y : item.y
        var bottom = onTop ? item.y + item.height : macos ? barTop : popupArea.y + popupArea.height
        return Qt.rect(item.x, top, item.width, bottom - top)
    }
    // Where the popover takes the pointer while a popup is open: all but the bars, so that a
    // press on either switches popups in one press. In the macOS style that is the output but the
    // menu bar's strip and the dock's rectangle, as the four rectangles around the dock; but
    // Launchpad covers both bars, and takes every press.
    readonly property var popoverInput: {
        if (!macos)
            return [popupArea]
        var top = menuBarHeight, w = popover.width, h = popover.height
        if (launcherOpen)
            return [Qt.rect(0, 0, w, h)]
        var left = bar.x, right = bar.x + bar.width, dockBottom = barTop + bar.height
        return [Qt.rect(0, top, w, barTop - top), Qt.rect(0, dockBottom, w, h - dockBottom),
                Qt.rect(0, barTop, left, bar.height), Qt.rect(right, barTop, w - right, bar.height)]
    }
    // An application's record in shell.apps for a window's app id: the application whose pinned
    // slot it takes (a configured launcher too), else the installed one; null when there is none.
    function appRecord(windowAppId) {
        var id = shell.pinnedAppFor(windowAppId) || shell.appFor(windowAppId)
        var apps = id !== "" ? shell.apps : []
        for (var i = 0; i < apps.length; ++i)
            if (apps[i].appId === id)
                return apps[i]
        return null
    }
    onLauncherOpenChanged: {
        if (launcherOpen) { taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = "" }
        else powerOpen = false
    }
    function closeMenus() {
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; trayMenuKey = ""; menuBarMenu = ""
        barKeys.leave()
    }
    // A change of style lays the panel out anew: what was open belonged to bars that are gone.
    onMacosChanged: closeMenus()
    // A stacked button hovered for a moment, or at once while another's list is open, shows its
    // windows; leaving both it and the list hides them again. With thumbnails any window's button
    // does, after shell.thumbnailDelay. Pressing the button hides them until the pointer comes
    // back.
    function showsWindows(button) { return button.stacked || thumbnails }
    function hoverGroup(button, hovered) {
        // The keyboard on the bar holds what it shows until the pointer moves (pointerTakesOver).
        if (barKeys.active)
            return
        if (hovered && showsWindows(button) && !menuOpen && !button.pressed) {
            groupPending = button
            groupHide.stop()
            if (groupOpen) {
                showGroup()
            } else {
                warmGroup = null
                groupShow.restart()
                if (thumbnails) pictureWarmup.restart()
            }
        } else if (!hovered && button === groupPending) {
            // Entering the next button can come before leaving this one.
            stopWaiting()
            if (groupOpen) groupHide.restart()
        }
    }
    // The pointer is no longer waiting for a button's windows to show.
    function stopWaiting() {
        groupShow.stop()
        pictureWarmup.stop()
        warmGroup = null
    }
    function closeGroup() {
        stopWaiting()
        groupOpen = false
    }
    function showGroup() {
        var button = groupPending
        if (button && button.hovered && !button.pressed && showsWindows(button) && !menuOpen)
            openGroup(button)
        warmGroup = null
    }
    // The windows a button shows, as {slot, windowApp, task} for the filters' app, windowApp and
    // taskId: a taskbar button without a group stands for its own window; a dock icon has none.
    function windowsOf(button) {
        return { slot: button.groupSlot, windowApp: button.groupWindowApp,
                 task: button.group === null ? button.taskId : -1 }
    }
    function openGroup(button) {
        var windows = windowsOf(button)
        groupTask = windows.task
        groupSlot = windows.slot
        groupWindowApp = windows.windowApp
        groupIcon = button.iconName
        groupX = button.mapToItem(root, button.width / 2, 0).x
        groupOpen = true
    }
    Timer { id: groupShow; interval: root.thumbnails ? shell.thumbnailDelay : 350; onTriggered: root.showGroup() }
    Timer {
        id: groupHide; interval: 300
        onTriggered: if (!root.groupListHovered && !(root.groupPending && root.groupPending.hovered) && !root.dragHolds &&
                         !root.barKeys.active) root.groupOpen = false
    }
    // The pointer entering the list (or the card) keeps it, and leaving it hides it a moment later.
    function hoverGroupList(hovered) {
        if (barKeys.active)
            return
        if (hovered) groupHide.stop(); else groupHide.restart()
    }
    // The pointer took the bar back from the keyboard (barKeys), on `button` or on none of the
    // bar's stops: what it is on goes on as on hover. The card stays while it is on it, glides to
    // a button with windows it is on, and closes otherwise.
    function pointerTakesOver(button) {
        stopWaiting()
        if (groupListHovered)
            return
        var shows = button && typeof button.dragWindows === "function" && button.dragWindows().length > 0 &&
                    showsWindows(button)
        if (shows && groupOpen) {
            groupPending = button
            openGroup(button)
            return
        }
        groupOpen = false
        if (shows)
            hoverGroup(button, true)
    }
    // A drag from an application (a file, text, a link) resting on a button with windows brings
    // them forward, as on Windows: after dragDelay a single window is activated (raised, restored,
    // its workspace shown) and a stack shows its windows, as on hover; resting on one of those
    // activates it in turn, and the drag goes on onto the window to drop there. A drag only
    // crossing a button does nothing. Its events go to the surface under the pointer, never to the
    // hover handlers, so a DropArea over the panel's surface and one over the popover's follow it
    // (dragOverBar, dragOverPopover). What it opened stays open while it is over the card or its
    // button (dragHolds), and closes a moment after it has left both or ended. Neither takes the
    // drop: the application hears that nothing here would, and it is cancelled.
    // Half a second, longer than the pointer rests for a stack's list (350 ms) or, by default, a
    // card (shell.thumbnailDelay): a drag crosses the bar on its way elsewhere more often than the
    // pointer does, and a window brought forward changes what lies under the drag, where a card
    // opened by mistake only covers a little of the screen. Windows waits about as long.
    readonly property int dragDelay: 500
    // The button with windows the drag is over, the tile or row of a window on the open card or
    // list it is over, null for none; and whether it is over the popover's card (or between it and
    // the bar).
    property Item dragButton: null
    property Item dragTile: null
    property bool dragOnCard: false
    readonly property bool dragHolds: groupOpen && (dragOnCard || dragButton !== null && showsWindowsOf(dragButton))
    onDragHoldsChanged: if (!dragHolds && groupOpen) groupHide.restart()
    function showsWindowsOf(button) {
        var windows = windowsOf(button)
        return windows.slot === groupSlot && windows.windowApp === groupWindowApp && windows.task === groupTask
    }
    // The topmost item at (x, y) in `reference`'s coordinates among `item`'s descendants that
    // `matches`, looking only inside items that hold the point; null for none.
    function itemUnder(reference, item, x, y, matches) {
        var children = item.children
        for (var i = children.length - 1; i >= 0; --i) {
            var child = children[i]
            if (!child.visible || !child.contains(child.mapFromItem(reference, x, y)))
                continue
            if (matches(child))
                return child
            var found = itemUnder(reference, child, x, y, matches)
            if (found)
                return found
        }
        return null
    }
    // The drag at `point` on the panel's surface, or gone from it (null). A button with windows
    // has dragWindows(); a pinned application's without any, or the dock's Trash, does nothing.
    function dragOverBar(point) {
        var button = point ? itemUnder(root, bar, point.x, point.y, function(item) {
            return typeof item.dragWindows === "function" && item.enabled
        }) : null
        var count = button ? button.dragWindows().length : 0
        if (count === 0)
            button = null
        if (button === dragButton)
            return
        dragButton = button
        dragWarmup.stop()
        if (!groupOpen)
            warmGroup = null
        if (!button || groupOpen && showsWindowsOf(button)) {
            dragRest.stop()
        } else if (groupOpen && count > 1) {
            // Open, the card or list goes over to another stack's windows at once, as on hover.
            dragRest.stop()
            openGroup(button)
        } else {
            dragRest.restart()
            if (thumbnails && count > 1)
                dragWarmup.restart()
        }
    }
    // The drag at `point` on the popover's surface, or gone from it (null).
    function dragOverPopover(point) {
        var popup = point && groupOpen ? groupPopup : null
        dragOnCard = popup !== null
        var tile = popup ? itemUnder(popupLayer, popup, point.x, point.y, function(item) {
            return item.taskId !== undefined && item.active !== undefined && item.minimized !== undefined
        }) : null
        if (tile === dragTile)
            return
        dragTile = tile
        if (tile) dragRest.restart(); else dragRest.stop()
    }
    // A menu opened meanwhile (by a key) has the keyboard, and keeps it.
    function springDrag() {
        if (menuOpen)
            return
        if (dragTile) {
            bringForward(dragTile)
            return
        }
        var windows = dragButton ? dragButton.dragWindows() : []
        if (windows.length === 1)
            bringForward(windows[0])
        else if (windows.length > 1)
            openGroup(dragButton)
        warmGroup = null
    }
    // Activating the window in front already would minimize it.
    function bringForward(window) {
        if (!window.active || window.minimized)
            taskSource.activate(window.taskId)
    }
    // Halfway into the delay, a stack's pictures are taken for its card, as on hover.
    function warmDragged() {
        if (!dragButton || groupOpen || !thumbnails)
            return
        warmGroup = windowsOf(dragButton)
        if (!picturesFit(warmWindows.count))
            warmGroup = null
    }
    Timer { id: dragRest; interval: root.dragDelay; onTriggered: root.springDrag() }
    Timer { id: dragWarmup; interval: root.dragDelay / 2; onTriggered: root.warmDragged() }
    // Opens on press, as a desktop context menu does: waiting for a tap lost a press held
    // past the long-press time or moved while held. The new menu opens before the old one
    // closes, so the popover stays up in between.
    function openContextMenu(item, x, taskId, app) {
        contextMenuX = item.mapToItem(root, x, 0).x
        taskMenuGroup = taskId >= 0 && item.stacked === true ? { slot: item.groupSlot, windowApp: item.groupWindowApp } : null
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
        launcherOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = ""
    }
    // Opens (or, when it is already open, closes) one of the volume control's popups.
    function toggleAudioPopup(kind, item) {
        if (audioPopup === kind) { audioPopup = ""; return }
        audioPopupX = item.mapToItem(root, item.width / 2, 0).x
        audioPopupWidth = item.width
        audioPopup = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; trayMenuKey = ""; menuBarMenu = ""
    }
    // Opens (or, when it is already open, closes) one of the menu bar's own menus below `item`.
    function toggleMenuBarMenu(kind, item) {
        if (menuBarMenu === kind) { menuBarMenu = ""; return }
        menuBarMenuX = item.mapToItem(root, 0, 0).x
        menuBarMenu = kind
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = ""
    }
    // While one of them is open, the pointer moving onto another's title opens that one instead.
    function hoverMenuBarMenu(kind, item) {
        if (menuBarMenu !== "" && menuBarMenu !== kind)
            toggleMenuBarMenu(kind, item)
    }
    // The power menu, from the power_menu action: the launcher opens with it, and closes with it
    // when asked again.
    function togglePowerMenu() {
        // The macOS style's system menu has them.
        if (macos) {
            if (menuBarMenu === "system")
                closeMenus()
            else if (menuBar && shell.power.entries.length > 0)
                toggleMenuBarMenu("system", menuBar.systemButton)
            return
        }
        if (powerOpen) {
            closeMenus()
        } else if (shell.power.entries.length > 0) {
            launcherOpen = true
            powerOpen = true
        }
    }
    // For `paw-shell --preview-popup NAME`, which shows one popup for a screenshot: opens it
    // the way its button would. "bar" opens none. Returns false for a name it does not know, or
    // when what the popup belongs to is not on the bar.
    function previewPopup(name) {
        if (macos) {
            var shown = previewMacos(name)
            if (shown !== undefined)
                return shown
        }
        var taskList = taskbar ? taskbar.tasks : null
        var tray = statusArea.trayRow, clockButton = statusArea.clock, audioWidget = statusArea.volume
        var quickButton = statusArea.quickSettings
        var profilesButton = statusArea.profiles, wallpapersButton = statusArea.wallpapers
        if (taskList)
            taskList.forceLayout()
        var i
        switch (name) {
        case "bar":
            return true
        case "launcher":
            launcherOpen = true
            return true
        case "launcher-all":
        case "launcher-search":
        case "launcher-empty":
        case "launcher-menu":
            // The start menu's other views, as Launcher.preview names them.
            launcherOpen = true
            return launcherLoader.item.preview(name)
        case "power":
            togglePowerMenu()
            return powerOpen
        case "bar-menu":
            openContextMenu(bar, bar.width / 2, -1)
            return true
        case "bar-submenu":
            // The appearance profiles beside the bar menu.
            openContextMenu(bar, bar.width / 2, -1)
            previewSubmenu.menu = contextMenuLoader
            previewSubmenu.start()
            return shell.profiles.length > 0
        case "task-menu":
            // The first window's button.
            for (i = 0; i < taskList.count; ++i) {
                var task = taskList.itemAtIndex(i)
                if (task && !task.launcher) {
                    openContextMenu(task, 0, task.taskId, task.appId)
                    return true
                }
            }
            return false
        case "pin-menu":
            // An installed application's launcher when there is one, whose menu has more to show.
            var launcher = null
            for (i = 0; i < taskList.count; ++i) {
                var button = taskList.itemAtIndex(i)
                if (button && button.launcher && (!launcher || launcher.app.configured && !button.app.configured))
                    launcher = button
            }
            if (launcher)
                openContextMenu(launcher, 0, -1, launcher.app)
            return launcher !== null
        case "stack-menu":
            // A stacked button's menu, with the workspaces to move its windows to beside it.
            for (i = 0; i < taskList.count; ++i) {
                var stack = taskList.itemAtIndex(i)
                if (stack && stack.stacked) {
                    openContextMenu(stack, 0, stack.taskId, stack.appId)
                    previewSubmenu.menu = contextMenuLoader
                    previewSubmenu.start()
                    return true
                }
            }
            return false
        case "group":
        case "thumbnails":
            // A stacked button: its list, or with shell.thumbnails the pictures of its windows (the
            // gallery's group turns them off).
            for (i = 0; i < taskList.count; ++i) {
                var stacked = taskList.itemAtIndex(i)
                if (stacked && stacked.stacked) {
                    openGroup(stacked)
                    return name === "group" || thumbnailsOpen
                }
            }
            return false
        case "keyboard":
            return previewKeyboard()
        case "tray-menu":
        case "tray-submenu":
            for (i = 0; i < tray.children.length; ++i)
                if (tray.children[i].hasMenu) {
                    trayMenu(tray.children[i])
                    if (name === "tray-submenu") {
                        previewSubmenu.menu = trayMenuLoader
                        previewSubmenu.start()
                    }
                    return true
                }
            return false
        case "calendar":
        case "clock-empty":
        case "calendar-years":
            // The flyout; with no notifications kept, or with the calendar zoomed out to the years.
            if (name === "clock-empty")
                shell.notifications.clearHistory()
            toggleAudioPopup("clock", clockButton)
            if (name === "calendar-years")
                Qt.callLater(function() { clockFlyoutLoader.item.calendar.view = "years" })
            return clockButton.visible
        case "mixer":
        case "outputs":
            toggleAudioPopup(name, audioWidget)
            return audioWidget.visible
        case "profiles":
            toggleAudioPopup("profiles", profilesButton)
            return profilesButton.visible
        case "wallpapers":
            toggleAudioPopup("wallpapers", wallpapersButton)
            return wallpapersButton.visible
        case "quick-settings":
        case "quick-settings-mixer":
        case "quick-settings-power":
        case "quick-settings-wifi":
        case "quick-settings-bluetooth":
            // Quick Settings; with the applications' volumes open, the power modes, the networks or
            // the Bluetooth devices.
            toggleAudioPopup("quick", quickButton)
            if (name === "quick-settings-mixer")
                Qt.callLater(function() { quickSettingsLoader.item.expanded = "mixer" })
            if (name === "quick-settings-power")
                Qt.callLater(function() { quickSettingsLoader.item.expanded = "powerMode" })
            if (name === "quick-settings-wifi")
                Qt.callLater(function() { quickSettingsLoader.item.expanded = "wifi" })
            if (name === "quick-settings-bluetooth")
                Qt.callLater(function() { quickSettingsLoader.item.expanded = "bluetooth" })
            return quickButton.visible
        case "wifi":
            // The network widget's networks, on the bar.
            toggleAudioPopup("wifi", statusArea.network)
            return statusArea.network.visible
        case "notifications":
            // The flyout with the mail application's notifications expanded.
            toggleAudioPopup("clock", clockButton)
            Qt.callLater(function() { clockFlyoutLoader.item.notifications.expanded = { "Mail": true } })
            return shell.notifications.serving
        }
        return false
    }
    // The popups previewPopup opens otherwise in the macOS style: those of the dock's icons and
    // the menu bar's menus (system-menu, app-menu, window-menu, window-submenu); undefined for
    // the others, which open as on the taskbar.
    function previewMacos(name) {
        var icons = [], i
        for (i = 0; i < dock.pins.count; ++i)
            icons.push(dock.pins.itemAt(i))
        for (i = 0; i < dock.running.count; ++i)
            icons.push(dock.running.itemAt(i))
        // The first icon with windows, with several, or without any.
        function find(test) { return icons.filter(function(icon) { return icon && test(icon) })[0] || null }
        var icon
        switch (name) {
        case "power":
        case "system-menu":
            toggleMenuBarMenu("system", menuBar.systemButton)
            return true
        case "app-menu":
            toggleMenuBarMenu("app", menuBar.appButton)
            return true
        case "window-menu":
        case "window-submenu":
            toggleMenuBarMenu("window", menuBar.windowButton)
            if (name === "window-submenu") {
                previewSubmenu.menu = menuBarMenuLoader
                previewSubmenu.start()
            }
            return true
        case "task-menu":
            icon = find(function(icon) { return icon.running })
            if (icon)
                dock.openMenu(icon, icon.modelData || null, "")
            return icon !== null
        case "pin-menu":
            icon = find(function(icon) { return !icon.running && icon.modelData && !icon.modelData.configured })
            if (icon)
                dock.openMenu(icon, icon.modelData, icon.modelData.appId)
            return icon !== null
        case "keyboard":
            return previewKeyboard()
        case "stack-menu":
        case "group":
        case "thumbnails":
            // The dock lists a stack's windows, pictures or not.
            icon = find(function(icon) { return icon.stacked })
            if (icon && name !== "stack-menu")
                openGroup(icon)
            else if (icon)
                dock.openMenu(icon, icon.modelData || null, "")
            return icon !== null
        }
        return undefined
    }
    // The keyboard on the bar (taskbar_focus) at a stack's second window: a picture on the card,
    // or a row of the list.
    function previewKeyboard() {
        if (taskbar)
            taskbar.tasks.forceLayout()
        barKeyboard.toggle()
        var list = barKeyboard.stops()
        for (var i = 0; i < list.length; ++i) {
            var item = barKeyboard.itemOf(list[i])
            if (item && item.stacked) {
                barKeyboard.select(list, i)
                barKeyboard.window = 1
                return groupOpen
            }
        }
        return false
    }
    // For a preview: opens the first submenu of a menu just opened, once its rows are laid out.
    Timer {
        id: previewSubmenu
        property Loader menu
        interval: 50
        onTriggered: {
            var entries = menu.item ? menu.item.entries : []
            for (var i = 0; i < entries.length; ++i)
                if (entries[i].submenu) {
                    menu.item.openSubmenu(i)
                    return
                }
        }
    }
    // Where a tray item's icon is on the screen, which some applications place a window by: the
    // panel spans its output's width, at its top or bottom edge, and the menu bar along its top.
    function trayPoint(item) {
        var p = item.mapToItem(root, item.width / 2, item.height / 2)
        var inMenuBar = item.Window.window === menuBarWindow
        return Qt.point(Math.round(Screen.virtualX + p.x),
                        Math.round(Screen.virtualY + (inMenuBar || onTop ? p.y : Screen.height - height + p.y)))
    }
    // An item that is only a menu opens it; one that cannot be activated opens it after all.
    property Item trayActivating: null
    function trayActivate(button) {
        if (button.itemIsMenu) {
            trayMenu(button)
            return
        }
        closeMenus()
        var p = trayPoint(button)
        trayActivating = button
        shell.tray.activate(button.key, p.x, p.y)
    }
    function traySecondary(button) {
        closeMenus()
        var p = trayPoint(button)
        shell.tray.secondaryActivate(button.key, p.x, p.y)
    }
    // Opens the item's menu by it, as the bar's own menus open: on press, the new menu before
    // the old one closes. A second press closes it. An item without a menu (to the panel) is
    // asked to show its own.
    function trayMenu(button) {
        if (!button.hasMenu) {
            closeMenus()
            var p = trayPoint(button)
            shell.tray.contextMenu(button.key, p.x, p.y)
            return
        }
        if (trayMenuKey === button.key) {
            closeMenus()
            return
        }
        trayMenuX = button.mapToItem(root, button.width / 2, 0).x
        trayMenuWidth = button.width
        trayMenuKey = button.key
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; menuBarMenu = ""
    }
    // The entries of item `key`'s menu entry `parent` (0 for the top), as a PopupMenu lists them:
    // an entry with children opens them beside it, read as it opens; any other is the
    // application's to carry out. `revision` only makes a binding read them again when it changes.
    function trayEntries(key, parent, revision) {
        if (key === "")
            return []
        var entries = shell.tray.menu(key, parent).map(function(entry) {
            if (entry.separator)
                return { separator: true }
            return { id: entry.id, text: entry.label, icon: entry.icon, enabled: entry.enabled,
                     toggle: entry.toggle, checked: entry.checked,
                     submenu: entry.submenu ? function() { return root.trayEntries(key, entry.id, root.trayMenuRevision) }
                                            : undefined,
                     run: entry.submenu ? undefined : function() { shell.tray.clickMenu(key, entry.id) } }
        })
        return entries.length > 0 ? entries : [{ text: "No entries", enabled: false }]
    }
    // The application is told which levels of its menu are on screen (AboutToShow and "opened"
    // for each that appears) and which no longer are ("closed", the deepest first), once a change
    // has settled: what it was told, as {key, id}.
    property var trayShown: []
    function syncTrayMenu() {
        var menu = trayMenuLoader.item
        var wanted = trayMenuKey === "" ? [] : [0].concat(menu ? menu.openEntries.map(function(entry) { return entry.id }) : [])
        wanted = wanted.map(function(id) { return { key: trayMenuKey, id: id } })
        function among(list, level) {
            return list.some(function(other) { return other.key === level.key && other.id === level.id })
        }
        for (var i = trayShown.length - 1; i >= 0; --i)
            if (!among(wanted, trayShown[i]))
                shell.tray.closeMenu(trayShown[i].key, trayShown[i].id)
        for (i = 0; i < wanted.length; ++i)
            if (!among(trayShown, wanted[i]))
                shell.tray.openMenu(wanted[i].key, wanted[i].id)
        trayShown = wanted
    }
    onTrayMenuKeyChanged: Qt.callLater(syncTrayMenu)
    Connections {
        target: shell.tray
        function onActivationRefused(key) {
            var button = root.trayActivating
            root.trayActivating = null
            if (button && button.key === key)
                root.trayMenu(button)
        }
        function onMenuChanged(key) {
            if (key !== root.trayMenuKey)
                return
            if (shell.tray.contains(key))
                root.trayMenuRevision++
            else
                root.trayMenuKey = ""
        }
    }
    // The overview, the window switcher and the command palette come up over the output's
    // windows, and the popups' surface is above them: what is open here closes, as the start
    // menu does on Windows.
    Connections {
        target: shell
        function onOverviewChanged() { if (shell.overviewOutput === root.outputName) root.closeMenus() }
        function onSwitcherChanged() { if (shell.switcherOutput === root.outputName) root.closeMenus() }
    }
    Connections {
        target: shell.palette
        function onOpenChanged() { if (shell.palette.output === root.outputName) root.closeMenus() }
    }
    function pinAction(appId) {
        // Reading shell.pinned re-evaluates the menu when pins change. Pinning waits until
        // the click is handled: the change rebuilds the menu, destroying the clicked item.
        var pinned = shell.pinned.some(function(app) { return app.appId === appId })
        var toggle = function() { Qt.callLater(function() { if (pinned) shell.unpin(appId); else shell.pin(appId) }) }
        if (macos)
            return { text: pinned ? "Remove from Dock" : "Keep in Dock", objectName: "contextMenuKeep", run: toggle }
        return pinned ? { text: "Unpin from taskbar", icon: "pin-off", run: toggle }
                      : { text: "Pin to taskbar", icon: "pin", run: toggle }
    }
    // Popups open away from the screen edge the bar sits on.
    // Tiling is per monitor: this panel shows and toggles its own.
    readonly property bool tiling: {
        var state = shell.workspaces[outputName]
        return state && state.tiling !== undefined ? state.tiling : shell.tiling
    }
    // The dock is at the bottom whatever shell.panel_position says.
    readonly property bool onTop: shell.panelTop && !macos

    // A click on the bar's empty space closes what is open.
    MouseArea {
        anchors.fill: parent
        visible: root.menuOpen
        onClicked: root.closeMenus()
    }
    // A drag over the panel's surface (dragDelay). Refusing every move refuses the drop: the
    // application hears that nothing here takes it. It fills the surface, so that it is the one
    // item a drag enters, once, and every answer after that is a move's.
    DropArea {
        anchors.fill: parent
        onEntered: (drag) => root.dragOverBar(Qt.point(drag.x, drag.y))
        onPositionChanged: (drag) => {
            drag.accepted = false
            root.dragOverBar(Qt.point(drag.x, drag.y))
        }
        onExited: root.dragOverBar(null)
    }

    // The popups' surface, over the whole output (PopoverWindow in view.hpp). While a menu is
    // open, or the bar has the keyboard, it takes the keyboard and every press but those on the
    // bar, and a press beside the popups closes them; the list shown on hover takes only the
    // pointer over it and up to the bar. It stays up while what closed fades out.
    PopoverWindow {
        id: popover
        panel: root.shellView
        keyboard: root.menuOpen || root.barKeys.active
        inputRects: root.menuOpen || root.barKeys.active
            ? root.popoverInput
            : root.groupOpen && root.groupPopup ? [root.hoverArea(root.groupPopup)] : []
        onDismissed: root.closeMenus()
        Timer { id: closing; interval: Theme.durationNormal; onTriggered: if (!root.expanded) popover.open = false }
        Connections {
            target: root
            function onExpandedChanged() {
                if (root.expanded) popover.open = true
                else closing.restart()
            }
        }

        Item {
            id: popupLayer
            anchors.fill: parent
            focus: true
            Keys.onEscapePressed: root.closeMenus()
            MouseArea {
                anchors.fill: parent
                enabled: root.menuOpen
                onPressed: root.closeMenus()
            }
            // A drag over the card or list of a button's windows, refused as over the bar.
            DropArea {
                anchors.fill: parent
                onEntered: (drag) => root.dragOverPopover(Qt.point(drag.x, drag.y))
                onPositionChanged: (drag) => {
                    drag.accepted = false
                    root.dragOverPopover(Qt.point(drag.x, drag.y))
                }
                onExited: root.dragOverPopover(null)
            }
            // The keyboard on the bar is held here (barKeys).
            BarKeyboard { id: barKeyboard; panel: root }
            // The pointer moving over the card, or between it and the bar, hands the bar back to
            // it from the keyboard; elsewhere over the popover it does not.
            HoverHandler {
                enabled: root.barKeys.active
                onPointChanged: {
                    var at = point.scenePosition
                    var area = root.groupOpen && root.groupPopup ? root.hoverArea(root.groupPopup) : null
                    if (hovered && area && at.x >= area.x && at.x < area.x + area.width && at.y >= area.y &&
                        at.y < area.y + area.height)
                        root.barKeys.pointerAt(at.x, at.y)
                }
            }

            // Left-clicking the volume control: the default output's volume, then each application's.
            PopupLoader {
                wanted: root.audioPopup === "mixer"
                early: root.warm
                sourceComponent: Component { AudioMixer { panel: root } }
            }

            // Clicking the clock: the notifications and a month calendar, at the bar's right end.
            PopupLoader {
                id: clockFlyoutLoader
                wanted: root.audioPopup === "clock"
                early: root.warm
                sourceComponent: Component { ClockFlyout { panel: root } }
            }

            // Right-clicking the volume control: the outputs to play through.
            PopupLoader {
                wanted: root.audioPopup === "outputs"
                early: root.warm
                sourceComponent: Component { AudioOutputs { panel: root } }
            }

            // The profile button: the appearance profiles, the one in use marked.
            PopupLoader {
                wanted: root.audioPopup === "profiles"
                early: root.warm
                sourceComponent: Component { ProfileList { panel: root } }
            }

            // The network widget's popup, where NetworkManager has Wi-Fi: the radio and the networks.
            PopupLoader {
                wanted: root.audioPopup === "wifi"
                sourceComponent: Component { WifiPopup { panel: root } }
            }

            // The wallpaper button: thumbnails of the pictures in shell.wallpapers, by subfolder, with
            // a filter; clicking one shows it at once and keeps the picker open to try another.
            PopupLoader {
                wanted: root.audioPopup === "wallpapers"
                sourceComponent: Component { WallpaperPicker { panel: root } }
            }

            PopupLoader {
                id: launcherLoader
                wanted: root.launcherOpen
                early: root.warm
                // The start menu, or Launchpad in the macOS style.
                sourceComponent: Theme.macos ? launchpadComponent : startMenuComponent
                Component { id: startMenuComponent; Launcher { panel: root } }
                Component { id: launchpadComponent; Launchpad { panel: root } }
            }

            PopupLoader {
                id: contextMenuLoader
                wanted: root.taskMenuId >= 0 || root.pinMenuApp !== null || root.barMenuOpen
                early: root.warm
                sourceComponent: Component { TaskbarMenu { panel: root } }
            }

            // A tray item's menu, in the style of the bar's own, its submenus beside it.
            PopupLoader {
                id: trayMenuLoader
                wanted: root.trayMenuKey !== ""
                early: root.warm
                sourceComponent: Component { TrayMenu { panel: root } }
            }

            // The windows of the hovered stacked button: clicking one focuses it (or minimizes it when
            // focused already), the cross or a middle click closes it, and a right click opens its menu.
            PopupLoader {
                id: groupListLoader
                wanted: root.groupOpen
                early: root.warm
                sourceComponent: Component { GroupList { panel: root } }
            }

            // The pictures of the windows of the button the pointer rests on, with shell.thumbnails
            // in the taskbar style.
            PopupLoader {
                id: thumbnailsLoader
                wanted: root.thumbnailsOpen
                early: root.thumbnails && (root.groupOpen || root.warm)
                sourceComponent: Component { WindowThumbnails { panel: root } }
            }

            // The Quick Settings button: tiles, the volume and brightness, the battery.
            PopupLoader {
                id: quickSettingsLoader
                wanted: root.audioPopup === "quick"
                early: root.warm
                sourceComponent: Component { QuickSettings { panel: root } }
            }

            // The menu bar's own menus in the macOS style: the system menu, the focused
            // application's and the Window menu.
            PopupLoader {
                id: menuBarMenuLoader
                wanted: root.menuBarMenu !== ""
                early: root.warm
                active: root.macos && (wanted || early || used)
                sourceComponent: Component { MenuBarMenu { panel: root } }
            }
            // A press anywhere hands the bar back to the pointer from the keyboard, and goes on to
            // what is under it: a picture is clicked, and beside the card it only closes it.
            MouseArea {
                anchors.fill: parent
                z: 1
                // Hidden, not only disabled, while the bar does not have the keyboard: a disabled
                // one still bears on where Qt delivers a press (one beyond the popover's edge, where
                // Launchpad's next page lies, missed it).
                visible: root.barKeys.active && !root.menuOpen
                acceptedButtons: Qt.AllButtons
                onPressed: (mouse) => {
                    root.barKeys.handOver()
                    mouse.accepted = false
                }
            }
        }
    }

    // The taskbar along the panel's edge, or the dock, as the style has it.
    Loader {
        id: taskbarLoader
        anchors.fill: parent
        active: !root.macos
        sourceComponent: Component { Taskbar { panel: root } }
    }
    Loader {
        id: dockLoader
        anchors.fill: parent
        active: root.macos
        sourceComponent: Component { Dock { panel: root } }
    }
    // The pointer moving over the bar hands it back from the keyboard (barKeys), as a press on it
    // does, which goes on to what is under it.
    HoverHandler {
        enabled: root.barKeys.active
        onPointChanged: if (hovered) root.barKeys.pointerAt(point.scenePosition.x, root.surfaceTop + point.scenePosition.y)
    }
    MouseArea {
        anchors.fill: parent
        z: 1
        visible: root.barKeys.active
        acceptedButtons: Qt.AllButtons
        onPressed: (mouse) => {
            root.barKeys.handOver()
            mouse.accepted = false
        }
    }
    // The panel's surface takes the pointer only over the dock (ShellView's inputRects): the
    // desktop beside it, and the room above it for an icon to bounce in, stay reachable.
    Binding {
        target: root.shellView
        property: "inputRects"
        value: root.dock ? [root.dock.area] : []
    }

    // The menu bar of the macOS style, along the output's top edge in a surface of its own
    // (MenuBarWindow in view.hpp), there while the style is macOS.
    MenuBarWindow {
        id: menuBarWindow
        panel: root.shellView
        shown: root.macos
        barHeight: Theme.menuBarHeight
        Loader {
            id: menuBarLoader
            anchors.fill: parent
            active: root.macos
            sourceComponent: Component { TopMenuBar { panel: root } }
        }
    }
}
