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
    // The middle of the item it opens by along the bar (alongBar), and the item's length there.
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
    // Every window, for which is active and which others there are (the bar's keyboard, the menu
    // bar and its menus).
    readonly property TaskFilter allWindows: TaskFilter { sourceModel: root.taskSource }
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
    onThumbnailsChanged: { closeGroup(); closeWorkspaceCard() }
    onWorkspaceCardsChanged: closeWorkspaceCard()
    readonly property int thumbnailGap: Theme.spacingS
    readonly property int thumbnailPadding: Theme.spacingM
    readonly property real thumbnailWidth: thumbnailWidthFor(groupWindows.count)
    function thumbnailWidthFor(count) {
        count = Math.max(1, count)
        var room = popupArea.width - 2 * Theme.spacingM - (count + 1) * thumbnailGap
        return Math.min(shell.thumbnailSize, Math.floor(room / count) - 2 * thumbnailPadding)
    }
    // Where windows show on this output (the task source's place, which a stand-in may not
    // have): `owner`'s rectangle, in the output's coordinates as the popover's are, for the
    // compositor to minimize the windows `taskIds` into and restore them out of. Of `layer` 1, a
    // picture on the card counts over the window's button while it is there.
    function placeWindows(owner, taskIds, rect, layer) {
        if (taskSource && typeof taskSource.place === "function")
            taskSource.place(owner, taskIds, outputName, rect, layer)
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
    // taken twice. A want may name the width in device pixels it wants the picture (0, or none,
    // for shell.thumbnailSize's), as the card of a workspace does, and a window is asked for at
    // the widest it is wanted: watched again at a new width before its last watch goes, so that
    // its capture goes on.
    property var pictureWants: ({})
    // The windows asked for, as {taskId: width}, and the source they were asked of.
    property var pictureWatches: ({})
    property var pictureSource: null
    function wantPicture(taskId, wanted, width) {
        width = width || 0
        var wants = pictureWants[taskId] || {}
        var count = (wants[width] || 0) + (wanted ? 1 : -1)
        if (count > 0) wants[width] = count
        else delete wants[width]
        if (Object.keys(wants).length > 0) pictureWants[taskId] = wants
        else delete pictureWants[taskId]
        Qt.callLater(syncPictures)
    }
    // The widest a window's picture is wanted, in device pixels.
    function wantedWidth(taskId) {
        var widest = 0
        for (var width in pictureWants[taskId])
            widest = Math.max(widest, Number(width) || Math.round(shell.thumbnailSize * Screen.devicePixelRatio))
        return widest
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
        for (id in pictureWants) {
            var width = wantedWidth(id)
            if (pictureWatches[id] === width)
                continue
            source.watchPicture(Number(id), width, shell.liveThumbnails)
            if (pictureWatches[id])
                source.unwatchPicture(Number(id))
            pictureWatches[id] = width
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
    // In the taskbar style with shell.thumbnails (and its `workspaces`), resting on a workspace of
    // the indicator for shell.thumbnailDelay shows a picture of it (WorkspaceCard.qml): its
    // wallpaper and the windows it shows where they are, each from its own picture. It opens,
    // glides to the next workspace the pointer rests on and closes as the card of a button's
    // windows does, and the two take turns: while one is open, the pointer moving onto the
    // other's buttons shows that one at once. From halfway into the delay the workspace is warmed
    // up (workspaceWarm): the card, made by then, lists its windows and wants their pictures, so
    // that it opens on them.
    readonly property bool workspaceCards: thumbnails && shell.workspaceThumbnails
    property bool workspaceCardOpen: false
    property int workspaceCardNumber: 0
    property real workspaceCardX: 0
    property int workspaceWarm: 0
    property Item workspacePending: null
    // A workspace's picture on its card: the output's proportions, one and a half times as wide as
    // a window's picture (shell.thumbnailSize) but no taller than 5/8 of that, as a monitor on its
    // side is narrower; `workspaceScale` card pixels to a logical pixel of the output. Its
    // windows' pictures are asked for at the width, in device pixels, whose box (5/8 of it tall)
    // holds the whole picture, and so any window of the output.
    readonly property size workspaceMiniature: {
        var widest = Math.round(shell.thumbnailSize * 1.5), tallest = Math.round(widest * 0.625)
        var width = popover.width, height = popover.height
        if (width <= 0 || height <= 0)
            return Qt.size(widest, tallest)
        var fit = Math.min(widest / width, tallest / height)
        return Qt.size(Math.round(width * fit), Math.round(height * fit))
    }
    readonly property real workspaceScale: popover.width > 0 ? workspaceMiniature.width / popover.width : 1
    readonly property int workspacePictureWidth: Math.ceil(Math.max(workspaceMiniature.width, workspaceMiniature.height / 0.625) *
                                                           Screen.devicePixelRatio)
    readonly property Item workspaceCard: workspaceCardLoader.item
    readonly property bool workspaceCardHovered: workspaceCard ? workspaceCard.hovered : false
    function hoverWorkspace(button, hovered) {
        // The keyboard on the bar holds what it shows until the pointer moves.
        if (barKeys.active || !workspaceCards)
            return
        if (hovered && !menuOpen && !button.pressed) {
            workspacePending = button
            workspaceHide.stop()
            if (workspaceCardOpen || groupOpen) {
                openWorkspaceCard(button)
            } else {
                workspaceWarm = 0
                workspaceShow.restart()
                workspaceWarmup.restart()
            }
        } else if (!hovered && button === workspacePending) {
            // Entering the next workspace can come before leaving this one.
            stopWaitingWorkspace()
            if (workspaceCardOpen) workspaceHide.restart()
        }
    }
    function stopWaitingWorkspace() {
        workspaceShow.stop()
        workspaceWarmup.stop()
        workspaceWarm = 0
    }
    function warmWorkspace() {
        var button = workspacePending
        if (workspaceShow.running && !workspaceCardOpen && button && button.hovered)
            workspaceWarm = button.number
    }
    function showWorkspaceCard() {
        var button = workspacePending
        if (button && button.hovered && !button.pressed && !menuOpen && workspaceCards)
            openWorkspaceCard(button)
        workspaceWarm = 0
    }
    function openWorkspaceCard(button) {
        closeGroup()
        workspaceCardNumber = button.number
        workspaceCardX = alongBar(button, lengthAlong(button) / 2)
        workspaceCardOpen = true
        workspaceWarm = 0
    }
    function closeWorkspaceCard() {
        stopWaitingWorkspace()
        workspaceCardOpen = false
    }
    // The pointer entering the card keeps it, and leaving it closes it a moment later.
    function hoverWorkspaceCard(hovered) {
        if (barKeys.active)
            return
        if (hovered) workspaceHide.stop(); else workspaceHide.restart()
    }
    // A click on the card: on a window, which it brings up (unless it is in front already, which
    // that would minimize), else anywhere, which shows the workspace.
    function pickWorkspace(number, window) {
        closeWorkspaceCard()
        if (window && window.taskId !== undefined && (!window.active || window.minimized))
            taskSource.activate(window.taskId)
        else if (number >= 1 && number <= shell.workspaceCount)
            shell.showWorkspace(outputName, number)
    }
    // A window's button, or its picture on the card of a button's windows, dragged onto a
    // workspace of the indicator and let go there moves the window to that workspace of this
    // output, as the overview's strip takes a thumbnail; a stack's button moves all its windows.
    // The workspace under the drag is lit (dropWorkspace), and what the drag carries (DropGhost)
    // drops into it. A button's drag stays in the bar's surface; a picture's is the popover's,
    // which takes the pointer over the whole output while it lasts (pictureDrag), and keeps the
    // card open.
    readonly property bool workspaceDrops: !macos && shell.workspaceCount > 1 && shell.widgets.workspaces &&
                                           !!taskSource && typeof taskSource.moveToWorkspace === "function"
    property int dropWorkspace: 0
    signal windowsDropped(int number)
    // The workspace of the indicator at (x, y) in the panel's coordinates, 0 for none.
    function workspaceAt(x, y) {
        if (!workspaceDrops)
            return 0
        var button = itemUnder(root, bar, x, y, function(item) { return item.dropsWindows === true })
        return button ? button.number : 0
    }
    // The middle of workspace `number`'s button, in the panel's coordinates.
    function workspacePoint(number) {
        var button = findItem(bar, function(item) { return item.dropsWindows === true && item.number === number })
        return button ? button.mapToItem(root, button.width / 2, button.height / 2) : Qt.point(0, 0)
    }
    // Moves the windows ({taskId} each) to workspace `number` of this output, through the output
    // first for one on another.
    function dropOnWorkspace(windows, number) {
        var all = allWindows.windows
        for (var i = 0; i < windows.length; ++i) {
            var id = windows[i].taskId, output = ""
            for (var j = 0; j < all.length; ++j)
                if (all[j].taskId === id)
                    output = all[j].output || ""
            if (output !== "" && output !== outputName && typeof taskSource.moveToOutput === "function")
                taskSource.moveToOutput(id, outputName)
            taskSource.moveToWorkspace(id, number)
        }
        windowsDropped(number)
    }
    // A window's picture being dragged from the card: {taskId, picture, icon}, null for none. It
    // ends with the drag, with its tile or its window going, or with closeMenus.
    property var pictureDrag: null
    Connections {
        target: root.allWindows
        function onCountChanged() {
            var drag = root.pictureDrag
            if (drag && !root.allWindows.windows.some(function(window) { return window.taskId === drag.taskId }))
                root.endPictureDrag(false)
        }
    }
    // The tile's picture `item` taken where it was pressed, the pointer at `at` now, both in the
    // popover's coordinates.
    function startPictureDrag(drag, item, pressed, at) {
        var from = item.mapToItem(popupLayer, 0, 0)
        dropGhost.picture = drag.picture || ""
        dropGhost.icon = drag.icon || ""
        dropGhost.size = Qt.size(item.width, item.height)
        dropGhost.hold = Qt.point(pressed.x - from.x, pressed.y - from.y)
        dropGhost.home = from
        pictureDrag = drag
        movePictureDrag(at.x, at.y)
    }
    function movePictureDrag(x, y) {
        dropWorkspace = workspaceAt(x - surfaceLeft, y - surfaceTop)
        if (dropWorkspace > 0)
            dropGhost.dock(x, y)
        else
            dropGhost.follow(x, y)
    }
    // Where what a drag carries docks while over a workspace, in the popover's coordinates: in
    // the middle of the workspace's picture while its card is open (resting there opens it, as
    // on hover), following the card as it glides; else beside the workspace's button, just off
    // the bar. dropDock keeps the last place while no workspace is under the drag, so that what
    // it carries, leaving one or dropped into it, never heads for anywhere else.
    property point dropDock
    onDropDockNowChanged: if (dropWorkspace > 0) dropDock = dropDockNow
    readonly property point dropDockNow: {
        var number = dropWorkspace, card = workspaceCard
        if (number <= 0)
            return dropDock
        if (card && workspaceCardOpen && workspaceCardNumber === number && card.visible) {
            void card.x
            void card.y
            void card.width
            void card.height
            void card.progress
            return card.miniatureItem.mapToItem(popupLayer, card.miniatureItem.width / 2, card.miniatureItem.height / 2)
        }
        return dockPoint(number)
    }
    // Beside workspace `number`'s button, just off the bar.
    function dockPoint(number) {
        var at = workspacePoint(number), gap = Theme.spacingM
        var x = at.x + surfaceLeft, y = at.y + surfaceTop
        var halfWidth = dropGhost.size.width * dropGhost.dockedScale / 2
        var halfHeight = dropGhost.size.height * dropGhost.dockedScale / 2
        if (vertical)
            x = edge === "left" ? barLeft + bar.width + gap + halfWidth : barLeft - gap - halfWidth
        else
            y = onTop ? barTop + bar.height + gap + halfHeight : barTop - gap - halfHeight
        return Qt.point(x, y)
    }
    // Let go, on a workspace (dropping it there) or not.
    function endPictureDrag(dropped) {
        var drag = pictureDrag, number = dropped && drag ? dropWorkspace : 0, at = dropDock
        dropWorkspace = 0
        pictureDrag = null
        if (number > 0) {
            dropGhost.land(at.x, at.y)
            dropOnWorkspace([{ taskId: drag.taskId }], number)
        } else {
            dropGhost.back(dropGhost.home.x, dropGhost.home.y)
        }
        if (!groupListHovered && groupOpen)
            groupHide.restart()
    }
    // A window's button dragged along the bar with the icon `icon`, the pointer at (x, y) in the
    // panel's coordinates: over a workspace, the icon on a card comes in docked by it. Returns
    // the workspace under it, 0 for none.
    function moveButtonDrag(icon, x, y) {
        dropWorkspace = workspaceAt(x, y)
        if (dropWorkspace > 0) {
            if (!dropGhost.carried) {
                dropGhost.picture = ""
                dropGhost.icon = icon
                dropGhost.size = Qt.size(Theme.appIconSizeLarge + 2 * Theme.spacingS, Theme.appIconSizeLarge + 2 * Theme.spacingS)
                dropGhost.hold = Qt.point(dropGhost.size.width / 2, dropGhost.size.height / 2)
            }
            dropGhost.dock(x + surfaceLeft, y + surfaceTop)
        } else if (dropGhost.carried) {
            dropGhost.back(dropGhost.x, dropGhost.y)
        }
        return dropWorkspace
    }
    // A drag let go: what it drops waits for the end of the event, as Qt says that a drag was
    // cancelled (something else taking the pointer) only after it says that it ended.
    property var pendingButtonDrop: null
    property bool pictureReleased: false
    function releaseButtonDrag(windows) {
        pendingButtonDrop = windows
        Qt.callLater(dropReleased)
    }
    function releasePictureDrag() {
        pictureReleased = true
        Qt.callLater(dropReleased)
    }
    function dropReleased() {
        var windows = pendingButtonDrop
        pendingButtonDrop = null
        if (windows)
            endButtonDrag(windows)
        if (pictureReleased && pictureDrag)
            endPictureDrag(true)
        pictureReleased = false
    }
    function cancelPictureDrag() {
        pictureReleased = false
        if (pictureDrag)
            endPictureDrag(false)
    }
    // The drag of a button ended without dropping anything: the workspace goes dark and what it
    // carried goes.
    function cancelButtonDrag() {
        pendingButtonDrop = null
        dropWorkspace = 0
        if (dropGhost.carried)
            dropGhost.back(dropGhost.x, dropGhost.y)
    }
    // Let go: the windows ({taskId} each) go to the workspace under it, if any, which it returns.
    function endButtonDrag(windows) {
        var number = dropWorkspace, at = dropDock
        dropWorkspace = 0
        if (number > 0) {
            dropGhost.land(at.x, at.y)
            dropOnWorkspace(windows, number)
        } else if (dropGhost.carried) {
            dropGhost.back(dropGhost.x, dropGhost.y)
        }
        return number
    }
    Timer { id: workspaceShow; interval: shell.thumbnailDelay; onTriggered: root.showWorkspaceCard() }
    Timer { id: workspaceWarmup; interval: shell.thumbnailDelay / 2; onTriggered: root.warmWorkspace() }
    Timer {
        id: workspaceHide; interval: 300
        onTriggered: if (!root.workspaceCardHovered && !(root.workspacePending && root.workspacePending.hovered) &&
                         !root.barKeys.active) root.workspaceCardOpen = false
    }
    // What shows the windows: the card of pictures or the list, and whether the pointer is on it.
    readonly property Item groupPopup: thumbnailsOpen ? thumbnailsLoader.item : groupListLoader.item
    readonly property bool groupListHovered: groupPopup ? groupPopup.hovered : false
    // Something is open in the popover, or the bar has the keyboard there, or is being dragged
    // or resized. The list shown on hover does not take the keyboard: it opens under a window
    // being typed in.
    readonly property bool expanded: menuOpen || groupOpen || workspaceCardOpen || barKeys.active || barHeld ||
                                     dropGhostShown
    // What a window dragged onto a workspace carries shows in the popover, over the bar.
    readonly property bool dropGhostShown: dropGhost.visible
    onMenuOpenChanged: {
        if (menuOpen) {
            stopWaiting()
            groupOpen = false
            closeWorkspaceCard()
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
    // The keyboard on the bar walks the buttons, not the workspaces: the card of one goes.
    Connections {
        target: root.barKeys
        function onActiveChanged() { if (root.barKeys.active) root.closeWorkspaceCard() }
    }
    // What the keyboard on the bar shows closing by itself (its last window gone, a menu opening
    // in its place) leaves the keyboard on its button.
    onGroupOpenChanged: if (!groupOpen && barKeys.active && !barKeys.menuOpened) barKeys.window = -1
    // The bars the style has: the taskbar (Taskbar.qml), or in the macOS style the dock
    // (Dock.qml) in the panel's surface and the menu bar (TopMenuBar.qml) in a surface of its own
    // along the output's top edge. While the output tiles, the tiling bar (TilingBar.qml,
    // shell.tiling_bar) stands in for them in the panel's surface, along its own edge, and
    // `macos` is false: the popups are placed as on the taskbar, drawn in the style's own way.
    // The style's bars are kept meanwhile, hidden and null here, so that tiling turned on and off
    // (or workspaces that tile and those that do not) switch bars at once.
    readonly property bool tilingBarShown: shell.tilingBar.enabled && tiling
    readonly property bool macos: Theme.macos && !tilingBarShown
    readonly property Item taskbar: tilingBarShown ? null : taskbarLoader.item
    readonly property Item dock: tilingBarShown ? null : dockLoader.item
    readonly property Item tilingBar: tilingBarLoader.item
    readonly property Item menuBar: tilingBarShown ? null : menuBarLoader.item
    readonly property MenuBarWindow menuBarWindow: menuBarWindow
    // The bar along the panel's edge, which its popups open by; the panel itself while it is
    // being made.
    readonly property Item bar: taskbar ? taskbar.barItem : dock ? dock.barItem : tilingBar ? tilingBar.barItem : root
    // The bar with the status widgets (the clock, Quick Settings, the tray, ...), whose popups
    // open by it, and the part that holds them and names the buttons they open by.
    readonly property Item statusBar: macos ? menuBar || root : bar
    readonly property Item statusArea: macos ? menuBar : taskbar || tilingBar
    // The surface the popups are drawn in, and the bar's top and left edges in its coordinates:
    // the bar's surface lies along its edge `edge`, across its width or along a side its height,
    // where it begins at surfaceLeft, surfaceTop.
    readonly property Item popupLayer: popupLayer
    // What Quick Settings opens the wallpaper picker by.
    readonly property Item quickSettingsButton: statusArea ? statusArea.quickSettings : null
    readonly property real surfaceTop: edge === "bottom" ? popover.height - height : 0
    readonly property real surfaceLeft: edge === "right" ? popover.width - width : 0
    readonly property real barTop: surfaceTop + bar.y
    readonly property real barLeft: surfaceLeft + bar.x
    // Where something on the bar is along it, in the popover's coordinates: its x, or its y along a
    // side, `offset` into it that way; and its length that way. The bar's surface and the popover
    // both begin at the output's edge across which the bar runs, so mapping to the panel will do.
    function alongBar(item, offset) {
        var point = item.mapToItem(root, vertical ? 0 : offset, vertical ? offset : 0)
        return vertical ? point.y : point.x
    }
    function lengthAlong(item) { return vertical ? item.height : item.width }
    // Where the bar's buttons begin, past the start button's inset, and where its widgets end,
    // which the start menu and the clock's and Quick Settings' cards line up with.
    readonly property real barStart: tilingBarShown ? shell.tilingBar.marginLeft
        : Theme.spacingL + (vertical ? shell.panelMarginTop : shell.panelMarginLeft)
    readonly property real barEnd: vertical ? barTop + bar.height : statusBar.x + statusBar.width
    // The menu bar's strip along the output's top edge, none without one.
    readonly property real menuBarHeight: macos ? Theme.menuBarHeight : 0
    // A popup of the bar opens away from the screen edge the bar is on (PopupCard's side), beside
    // the rectangle barAnchor gives: from `along` (alongBar), `length` long, and across the bar.
    // In the macOS style the bar with the widgets is the menu bar, and the dock's popups (an
    // application's menu, the windows of one) open above the dock by dockAnchor, which on the
    // taskbar is the bar's.
    readonly property int popupSide: macos ? Qt.BottomEdge
        : edge === "top" ? Qt.BottomEdge : edge === "left" ? Qt.RightEdge : edge === "right" ? Qt.LeftEdge : Qt.TopEdge
    function barAnchor(along, length) { return macos ? Qt.rect(along, 0, length, menuBarHeight) : dockAnchor(along, length) }
    // Where Quick Settings and the clock's flyout open: lined up with the bar's right end, as on
    // Windows; on the tiling bar, whose modules may stand anywhere along it, centred on the one
    // opening them.
    readonly property rect flyoutAnchor: tilingBarShown ? barAnchor(audioPopupX - audioPopupWidth / 2, audioPopupWidth)
                                                        : barAnchor(barEnd, 0)
    readonly property int flyoutAlignment: tilingBarShown ? Qt.AlignHCenter : Qt.AlignRight
    readonly property int dockSide: macos ? Qt.TopEdge : popupSide
    function dockAnchor(along, length) {
        return vertical ? Qt.rect(barLeft, along, bar.width, length) : Qt.rect(along, barTop, length, bar.height)
    }
    // The output but the bar's strip, where popups stay (PopupCard's bounds); between the menu
    // bar and the dock's strip in the macOS style.
    readonly property rect popupArea: macos
        ? Qt.rect(0, menuBarHeight, popover.width, popover.height - menuBarHeight - shell.panelExtent)
        : vertical ? Qt.rect(edge === "left" ? width : 0, 0, popover.width - width, popover.height)
        : Qt.rect(0, onTop ? height : 0, popover.width, popover.height - height)
    // Where a list shown on hover takes the pointer: over it and across to the bar, so that the
    // pointer crossing from its button never lands on a window between (which, with focus
    // following the pointer, would take the keyboard).
    function hoverArea(item) {
        if (vertical) {
            var left = edge === "left" ? popupArea.x : item.x
            var right = edge === "left" ? item.x + item.width : popupArea.x + popupArea.width
            return Qt.rect(left, item.y, right - left, item.height)
        }
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
            return launcherOpen && Theme.macos ? [Qt.rect(0, 0, popover.width, popover.height)] : [popupArea]
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
        // Read again as the applications change.
        void shell.appsRevision
        var id = shell.pinnedAppFor(windowAppId) || shell.appFor(windowAppId)
        return id !== "" ? shell.appRecord(id) || null : null
    }
    onLauncherOpenChanged: {
        if (launcherOpen) { taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = "" }
        else powerOpen = false
    }
    function closeMenus() {
        launcherOpen = false; taskMenuId = -1; pinMenuApp = null; barMenuOpen = false; audioPopup = ""; groupOpen = false; trayMenuKey = ""; menuBarMenu = ""
        closeWorkspaceCard()
        if (pictureDrag)
            endPictureDrag(false)
        barKeys.leave()
    }
    // A change of style lays the panel out anew: what was open belonged to bars that are gone. So
    // does the tiling bar coming or going.
    onMacosChanged: closeMenus()
    onTilingBarShownChanged: closeMenus()
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
            // Open, the card of a workspace gives way to this one at once.
            if (groupOpen || workspaceCardOpen) {
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
        closeWorkspaceCard()
        var windows = windowsOf(button)
        groupTask = windows.task
        groupSlot = windows.slot
        groupWindowApp = windows.windowApp
        groupIcon = button.iconName
        groupX = alongBar(button, lengthAlong(button) / 2)
        groupOpen = true
    }
    Timer { id: groupShow; interval: root.thumbnails ? shell.thumbnailDelay : 350; onTriggered: root.showGroup() }
    Timer {
        id: groupHide; interval: 300
        onTriggered: if (!root.groupListHovered && !(root.groupPending && root.groupPending.hovered) && !root.dragHolds &&
                         !root.barKeys.active && !root.pictureDrag) root.groupOpen = false
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
    // The first of `item`'s descendants that `matches`, depth first; null for none.
    function findItem(item, matches) {
        var children = item.children
        for (var i = 0; i < children.length; ++i) {
            if (matches(children[i]))
                return children[i]
            var found = findItem(children[i], matches)
            if (found)
                return found
        }
        return null
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
    // closes, so the popover stays up in between. It opens by `along` into the item along the bar.
    function openContextMenu(item, along, taskId, app) {
        contextMenuX = alongBar(item, along)
        taskMenuGroup = taskId >= 0 && item.stacked === true ? { slot: item.groupSlot, windowApp: item.groupWindowApp } : null
        if (taskId >= 0) { taskMenuId = taskId; taskMenuApp = shell.appFor(app || ""); pinMenuApp = null; barMenuOpen = false }
        else if (app) { pinMenuApp = app; taskMenuId = -1; barMenuOpen = false }
        else { barMenuOpen = true; taskMenuId = -1; pinMenuApp = null }
        launcherOpen = false; audioPopup = ""; trayMenuKey = ""; menuBarMenu = ""
    }
    // Opens (or, when it is already open, closes) one of the volume control's popups.
    function toggleAudioPopup(kind, item) {
        if (audioPopup === kind) { audioPopup = ""; return }
        audioPopupX = alongBar(item, lengthAlong(item) / 2)
        audioPopupWidth = lengthAlong(item)
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
            openContextMenu(bar, lengthAlong(bar) / 2, -1)
            return true
        case "bar-submenu":
            // The appearance profiles beside the bar menu.
            openContextMenu(bar, lengthAlong(bar) / 2, -1)
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
        case "workspace":
            // The card of the second workspace, with the windows the stand-ins put there.
            var workspaceButton = findItem(bar, function(item) { return item.objectName === "workspace2" })
            if (workspaceButton && workspaceCards)
                openWorkspaceCard(workspaceButton)
            return workspaceCardOpen
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
    // panel lies along its output's edge (surfaceLeft, surfaceTop), and the menu bar along its top.
    function trayPoint(item) {
        var p = item.mapToItem(root, item.width / 2, item.height / 2)
        var inMenuBar = item.Window.window === menuBarWindow
        return Qt.point(Math.round(Screen.virtualX + (inMenuBar ? p.x : surfaceLeft + p.x)),
                        Math.round(Screen.virtualY + (inMenuBar ? p.y : surfaceTop + p.y)))
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
        trayMenuX = alongBar(button, lengthAlong(button) / 2)
        trayMenuWidth = lengthAlong(button)
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
    // The output's edge the bar is along (shell.panelEdge; the dock is at the bottom whatever
    // shell.panel_position says, and the tiling bar where shell.tiling_bar says), and whether
    // that is a side, where the bar stands upright and things on it are placed by their y rather
    // than their x.
    readonly property string edge: tilingBarShown ? (shell.tilingBar.top ? "top" : "bottom") : shell.panelEdge
    readonly property bool vertical: !tilingBarShown && shell.panelVertical
    readonly property bool onTop: edge === "top"
    // Taskbar buttons show only the window's icon, its title as a tooltip: with shell.iconsOnly,
    // and always down a side, where no title fits.
    readonly property bool iconsOnly: shell.iconsOnly || vertical

    // Dragging the bar to another edge (shell.panel_draggable): a press on its empty space that
    // moves starts it here (startBarDrag), the compositor taking the pointer, and every panel's
    // popover then shows where the bar would go, the edge nearest the pointer on the output it is
    // over, taking no input; letting go moves it there (shell.movePanel), and Escape leaves it.
    // Resizing it by its inner edge (the same setting) starts with a press there
    // (startBarResize), the compositor taking the pointer too, and every bar is as tall as
    // shell.panelHeight says as the edge is dragged (as wide down a side); letting go keeps that
    // (shell.resizePanel), and Escape puts the old size back. The panel either started on holds
    // the keyboard meanwhile, for Escape.
    readonly property bool barDragging: shell.barDragging
    readonly property bool barResizing: shell.barResizing
    readonly property bool barHeld: barDragging || barResizing
    property bool barDragOrigin: false
    onBarHeldChanged: {
        if (!barHeld)
            barDragOrigin = false
        else if (barDragOrigin)
            popupLayer.forceActiveFocus()
    }
    function startBarDrag() {
        if (!shell.panelDraggable || barHeld)
            return
        closeMenus()
        barDragOrigin = true
        shell.startBarDrag()
    }
    // The bar's inner edge pressed at `point`, in the panel's coordinates: how far that is from
    // the output's edge the bar is along, from which the bar follows the pointer.
    function startBarResize(point) {
        if (!shell.panelDraggable || barHeld)
            return
        closeMenus()
        barDragOrigin = true
        shell.startBarResize(Math.round(edge === "top" ? point.y : edge === "bottom" ? height - point.y
                                        : edge === "left" ? point.x : width - point.x))
    }

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
        keyboard: root.menuOpen || root.barKeys.active || root.barDragOrigin && root.barHeld
        inputRects: root.pictureDrag ? [Qt.rect(0, 0, popover.width, popover.height)]
            : root.menuOpen || root.barKeys.active
            ? root.popoverInput
            : root.groupOpen && root.groupPopup ? [root.hoverArea(root.groupPopup)]
            : root.workspaceCardOpen && root.workspaceCard ? [root.hoverArea(root.workspaceCard)] : []
        onDismissed: {
            shell.cancelBarDrag()
            root.closeMenus()
        }
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
            Keys.onEscapePressed: {
                shell.cancelBarDrag()
                root.closeMenus()
            }
            // Where a dragged bar would go: the strip it would take along the edge the pointer is
            // nearest, inset by shell.panel_margin as the bar is.
            Rectangle {
                objectName: "barDropPreview"
                readonly property string edge: shell.barDragEdge
                readonly property bool upright: edge === "left" || edge === "right"
                visible: root.barDragging
                x: edge === "right" ? parent.width - width - shell.panelMarginRight : shell.panelMarginLeft
                y: edge === "bottom" ? parent.height - height - shell.panelMarginBottom : shell.panelMarginTop
                width: upright ? shell.panelHeight : parent.width - shell.panelMarginLeft - shell.panelMarginRight
                height: upright ? parent.height - shell.panelMarginTop - shell.panelMarginBottom : shell.panelHeight
                radius: Math.max(shell.panelRadius, Theme.radiusSmall)
                color: Theme.alpha(Theme.accent, 0.25)
                border.width: 2; border.color: Theme.accent
                Behavior on x { enabled: root.barDragging; NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                Behavior on y { enabled: root.barDragging; NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                Behavior on width { enabled: root.barDragging; NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                Behavior on height { enabled: root.barDragging; NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
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
            // What a window dragged onto a workspace carries, its picture or its icon, and where
            // a picture came from.
            DropGhost {
                id: dropGhost
                objectName: "dropGhost"
                property point home
                dockX: root.dropDock.x
                dockY: root.dropDock.y
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

            // The picture of the workspace the pointer rests on in the indicator, with
            // shell.thumbnails in the taskbar style; made as the pointer starts resting there.
            PopupLoader {
                id: workspaceCardLoader
                wanted: root.workspaceCardOpen || root.workspaceWarm > 0
                early: root.workspaceCards && root.warm
                active: root.workspaceCards && (wanted || early || used)
                sourceComponent: Component { WorkspaceCard { panel: root } }
            }

            // The pictures of the windows of the button the pointer rests on, with shell.thumbnails
            // in the taskbar style.
            PopupLoader {
                id: thumbnailsLoader
                wanted: root.thumbnailsOpen
                early: root.thumbnails && (root.groupOpen || root.warm)
                sourceComponent: Component { WindowThumbnails { panel: root } }
            }

            // The tiling bar's power module: the power menu by it.
            PopupLoader {
                wanted: root.audioPopup === "power"
                sourceComponent: Component {
                    PowerMenu {
                        objectName: "barPowerMenu"
                        panel: root
                        parent: root.popupLayer
                        open: root.audioPopup === "power"
                        anchorRect: root.barAnchor(root.audioPopupX - root.audioPopupWidth / 2, root.audioPopupWidth)
                        side: root.popupSide
                        alignment: Qt.AlignRight
                        bounds: root.popupArea
                        onDismissed: root.audioPopup = ""
                    }
                }
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

    // The taskbar along the panel's edge, or the dock, as the style has it, hidden while the
    // tiling bar stands in for them as the output tiles.
    Loader {
        id: taskbarLoader
        anchors.fill: parent
        active: !Theme.macos
        visible: !root.tilingBarShown
        sourceComponent: Component { Taskbar { panel: root } }
    }
    Loader {
        id: dockLoader
        anchors.fill: parent
        active: Theme.macos
        visible: !root.tilingBarShown
        sourceComponent: Component { Dock { panel: root } }
    }
    Loader {
        id: tilingBarLoader
        anchors.fill: parent
        active: root.tilingBarShown
        sourceComponent: Component { TilingBar { panel: root } }
    }
    // The pointer moving over the bar hands it back from the keyboard (barKeys), as a press on it
    // does, which goes on to what is under it.
    HoverHandler {
        enabled: root.barKeys.active
        onPointChanged: if (hovered) root.barKeys.pointerAt(root.surfaceLeft + point.scenePosition.x,
                                                            root.surfaceTop + point.scenePosition.y)
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
    // desktop beside it, and the room above it for an icon to bounce in, stay reachable. The
    // taskbar takes it over the strip it reserves, and not in the room past it that its shadow
    // falls into.
    Binding {
        target: root.shellView
        property: "inputRects"
        value: root.dock ? [root.dock.area]
             : root.tilingBarShown || shell.panelHeadroom <= 0 ? []
             : root.vertical ? [Qt.rect(root.edge === "left" ? 0 : shell.panelHeadroom, 0, shell.panelExtent, root.height)]
             : [Qt.rect(0, root.onTop ? 0 : shell.panelHeadroom, root.width, shell.panelExtent)]
    }

    // The surface lies along the tiling bar's edge, as tall as the strip it reserves, while it
    // shows.
    Binding {
        target: root.shellView
        property: "tilingBar"
        value: root.tilingBarShown
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
            active: Theme.macos
            sourceComponent: Component { TopMenuBar { panel: root } }
        }
    }
}
