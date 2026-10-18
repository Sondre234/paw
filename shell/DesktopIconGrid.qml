// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// The desktop's icons (shell.desktop_icons, the model shell.desktopIcons) on a grid in `area`, the
// room the bars leave, on the main monitor (`active`), and the presses and keys on the desktop
// around them. A click selects an icon, Ctrl + click or the middle button picks one more or puts
// one back, Shift + click takes the icons in the rectangle from the one clicked before, and a drag
// across the desktop selects every icon its band touches (with Ctrl or Shift, adding them): a band
// in the accent colour fading into a second hue, with rounded corners and a bubble at the pointer
// counting what it holds, each icon it catches hopping as it does. Dragged, the selected icons
// gather under the pointer into a stack with their count, while dashed pads on the grid show the
// cells they will land in, gliding from cell to cell; let go, they glide into those cells, the
// furthest last. On Wayland the drag is the compositor's, so that it reaches any window: the
// icons stay in their cells, faded, while it carries a picture of their stack (DesktopDragStack)
// with the pointer, the files going to the window they are dropped on; over the desktop its
// events (dropArea) move the pads, and let go there the icons come out of the stack where it was.
// Shown and hidden from the desktop's menu, the icons ripple out from where it was opened.
//
// Clicked, the desktop has the keyboard (ShellView.keyboardOnDemand). The arrows move the current
// icon, which a ring in the second hue shows as it hops from icon to icon, with Shift taking the
// icons between, with Ctrl leaving the selection as it is (Ctrl + Space picks one); letters typed
// go to the next icon whose name starts with them, said in a bubble over it. Enter opens, Delete
// moves to the Trash, F2 renames, Ctrl + A selects all, Ctrl + C copies, the menu key opens the
// icons' menu and Escape lets the selection go. A name is changed on its chip, which turns into a
// field (`renaming`); a new folder pops in with a puff of sparkles under where the menu was opened,
// its name there to type at once.
FocusScope {
    id: grid
    objectName: "desktopIconGrid"
    focus: true
    // The room on the desktop the bars leave, in its coordinates.
    property rect area
    // Whether this output's desktop has the icons: the main monitor's.
    property bool active: false
    readonly property var model: shell.desktopIcons
    readonly property bool shown: active && model.shown
    readonly property int iconSize: model.iconSize
    // A cell: the icon, the gap under it and two lines of its name, with room around.
    readonly property int cellWidth: iconSize + Math.max(40, Math.round(iconSize * 0.9))
    readonly property int cellHeight: iconSize + Math.ceil(nameFont.height * 2) + 2 * Theme.spacingM
                                      + Theme.spacingS + 2 * Theme.spacingXS + Theme.spacingS
    readonly property int margin: Theme.spacingL
    readonly property int columns: Math.max(1, Math.floor((area.width - 2 * margin) / cellWidth))
    readonly property int rows: Math.max(1, Math.floor((area.height - 2 * margin) / cellHeight))
    function cellX(column) { return area.x + margin + column * cellWidth }
    function cellY(row) { return area.y + margin + row * cellHeight }
    // The accent's partner: its hue a third of the way round the wheel (a blue's pink), for the
    // band's far side and the stack's count.
    readonly property color partner: Theme.accent.hslHue < 0 ? Theme.accent
        : Qt.hsla((Theme.accent.hslHue + 0.3) % 1, Math.max(0.45, Theme.accent.hslSaturation),
                  Theme.accent.hslLightness, 1)

    // A press anywhere on the desktop, for its menus to close; a right press beside the icons, and
    // one on the selection, for its menus to open there.
    signal pressed()
    signal menuRequested(real x, real y, bool byKeyboard)
    signal iconMenuRequested(real x, real y, bool byKeyboard)

    FontMetrics { id: nameFont; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall }

    // The grid is said once the desktop's size has settled: a new icon keeps the cell it is given
    // first, which a size the window passes through on its way would leave it in.
    Timer {
        id: layOut
        interval: 100
        onTriggered: if (grid.active && grid.area.width > 0 && grid.area.height > 0)
                         grid.model.setGrid(grid.columns, grid.rows)
    }
    onColumnsChanged: layOut.restart()
    onRowsChanged: layOut.restart()
    onActiveChanged: layOut.restart()
    Component.onCompleted: layOut.restart()

    // The selected icons' keys, as a set and in the order they were selected.
    property var selection: ({})
    property var selectionOrder: []
    function select(keys) {
        const set = {}
        for (const key of keys)
            set[key] = true
        selection = set
        selectionOrder = keys.slice()
    }
    function toggle(key) {
        select(selection[key] ? selectionOrder.filter(k => k !== key) : selectionOrder.concat([key]))
    }
    // The icons on the grid, and those selected, in the order they were.
    function tiles() {
        const list = []
        for (let i = 0; i < icons.count; ++i)
            if (icons.itemAt(i))
                list.push(icons.itemAt(i))
        return list
    }
    function selectedTiles() {
        const all = tiles()
        return selectionOrder.map(key => all.find(tile => tile.key === key)).filter(tile => !!tile)
    }
    function tileOf(key) {
        for (let i = 0; i < icons.count; ++i)
            if (icons.itemAt(i) && icons.itemAt(i).key === key)
                return icons.itemAt(i)
        return null
    }
    // The icons in their cells, down each column before the next, as they fill the grid.
    function ordered() {
        return tiles().filter(tile => tile.placed).sort((a, b) => a.column - b.column || a.row - b.row)
    }
    // An icon gone from the folder leaves the selection, and the keyboard goes on to the icon
    // nearest where it was.
    Connections {
        target: grid.model
        function onCountChanged() {
            const kept = grid.selectedTiles().map(tile => tile.key)
            if (kept.length !== grid.selectionOrder.length)
                grid.select(kept)
            if (grid.current === "" || grid.currentTile)
                return
            let nearest = null
            for (const tile of grid.ordered())
                if (!nearest || Math.hypot(tile.column - grid.currentCell.x, tile.row - grid.currentCell.y) <
                                Math.hypot(nearest.column - grid.currentCell.x, nearest.row - grid.currentCell.y))
                    nearest = tile
            grid.current = nearest ? nearest.key : ""
        }
    }

    // The keyboard's current icon (its key) and its cell, the icon Shift takes those between from,
    // and whether the keys are in use, which shows the ring: from a key moving it until the
    // pointer is pressed.
    property string current: ""
    property point currentCell
    property string extendFrom: ""
    property bool keysUsed: false
    readonly property Item currentTile: { current; icons.count; return tileOf(current) }
    readonly property bool ringShown: keysUsed && activeFocus && renaming === "" && !dragging &&
                                      currentTile !== null && currentTile.visible
    onCurrentTileChanged: if (currentTile) currentCell = Qt.point(currentTile.column, currentTile.row)
    // The keys take `tile` as the current icon: alone in the selection, with those between it and
    // `extendFrom` (`extend`), or leaving the selection as it is (`keep`).
    function moveTo(tile, extend, keep) {
        if (extend && (extendFrom === "" || !tileOf(extendFrom)))
            extendFrom = currentTile ? current : tile.key
        current = tile.key
        keysUsed = true
        if (extend) {
            selectBetween(extendFrom, tile.key)
        } else if (!keep) {
            select([tile.key])
            extendFrom = tile.key
        }
    }
    // Selects the icons in the rectangle of cells from `fromKey`'s to `toKey`'s, `fromKey`'s first.
    function selectBetween(fromKey, toKey) {
        const from = tileOf(fromKey), to = tileOf(toKey)
        if (!from || !to)
            return
        const left = Math.min(from.column, to.column), right = Math.max(from.column, to.column)
        const top = Math.min(from.row, to.row), bottom = Math.max(from.row, to.row)
        const inside = ordered().filter(tile => tile.column >= left && tile.column <= right &&
                                                tile.row >= top && tile.row <= bottom && tile.key !== fromKey)
        select([fromKey].concat(inside.map(tile => tile.key)))
    }
    // An arrow: the nearest icon that way, one level with the current first (in its row, or in its
    // column), then by the distance between their cells. The first key of all lands on the current
    // icon, or the first.
    function step(dx, dy, extend, keep) {
        const from = currentTile
        if (!from || !from.placed || (!keysUsed && selectionOrder.length === 0)) {
            const first = from && from.placed ? from : ordered()[0]
            if (first)
                moveTo(first, false, keep)
            return
        }
        let best = null, bestLevel = false, bestDistance = 0
        for (const tile of ordered()) {
            const along = dx !== 0 ? (tile.column - from.column) * dx : (tile.row - from.row) * dy
            if (tile === from || along <= 0)
                continue
            const level = dx !== 0 ? tile.row === from.row : tile.column === from.column
            const distance = Math.hypot(tile.column - from.column, tile.row - from.row)
            if (!best || (level && !bestLevel) || (level === bestLevel && distance < bestDistance)) {
                best = tile
                bestLevel = level
                bestDistance = distance
            }
        }
        if (best)
            moveTo(best, extend, keep)
        keysUsed = true
    }
    // What Enter opens: the selected icons, else the current one.
    function chosen() {
        return selectionOrder.length > 0 ? selectedTiles() : currentTile ? [currentTile] : []
    }
    // The icons' menu from the keyboard, beside the current icon, which it takes alone when
    // nothing is selected; the desktop's, with no icons, at their corner.
    function keyMenu() {
        if (selectionOrder.length === 0 && currentTile)
            select([current])
        const tile = selectionOrder.indexOf(current) >= 0 ? currentTile : selectedTiles()[0]
        if (tile)
            iconMenuRequested(tile.x + tile.width / 2, tile.y + tile.height - Theme.spacingS, true)
        else
            menuRequested(area.x + margin, area.y + margin, true)
    }

    // Type-ahead: what was typed in the last second, said in a bubble over the icon it found.
    property string typed: ""
    Timer { id: typing; interval: 1000; onTriggered: grid.typed = "" }
    // Letters typed go to the next icon whose name starts with them, from the current icon on; one
    // letter again and again steps through those starting with it.
    function typeAhead(text) {
        const next = typed + text
        typed = next
        typing.restart()
        const lower = next.toLocaleLowerCase()
        const again = lower.length > 1 && lower.split("").every(c => c === lower[0])
        const prefix = again ? lower[0] : lower
        const list = ordered()
        const at = list.findIndex(tile => tile.key === current)
        // A new letter looks past the current icon; more of a name may stay on it.
        const from = at < 0 ? 0 : next.length === 1 || again ? at + 1 : at
        for (let i = 0; i < list.length; ++i) {
            const tile = list[(from + i) % list.length]
            if (tile.name.toLocaleLowerCase().startsWith(prefix)) {
                moveTo(tile, false, false)
                return
            }
        }
        keysUsed = true
    }

    Keys.onPressed: function(event) {
        if (!grid.shown || grid.renaming !== "")
            return
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
        switch (event.key) {
        case Qt.Key_Left: grid.step(-1, 0, shift, ctrl); break
        case Qt.Key_Right: grid.step(1, 0, shift, ctrl); break
        case Qt.Key_Up: grid.step(0, -1, shift, ctrl); break
        case Qt.Key_Down: grid.step(0, 1, shift, ctrl); break
        case Qt.Key_Home:
        case Qt.Key_End: {
            const list = grid.ordered()
            if (list.length > 0)
                grid.moveTo(list[event.key === Qt.Key_Home ? 0 : list.length - 1], shift, ctrl)
            break
        }
        case Qt.Key_Return:
        case Qt.Key_Enter: grid.open(grid.chosen()); break
        case Qt.Key_Delete: grid.trash(grid.selectedTiles()); break
        case Qt.Key_F2: {
            const one = grid.selectionOrder.length === 1 ? grid.selectionOrder[0]
                      : grid.selectionOrder.length === 0 ? grid.current : ""
            grid.startRename(one)
            break
        }
        case Qt.Key_Menu: grid.keyMenu(); break
        case Qt.Key_F10:
            if (!shift)
                return
            grid.keyMenu()
            break
        case Qt.Key_Escape:
            if (grid.selectionOrder.length === 0 && grid.typed === "")
                return
            grid.select([])
            grid.typed = ""
            break
        case Qt.Key_Space:
            if (ctrl && grid.currentTile) {
                grid.toggle(grid.current)
                grid.extendFrom = grid.current
                grid.keysUsed = true
            } else if (plain && grid.typed !== "") {
                grid.typeAhead(" ")
            } else {
                return
            }
            break
        default:
            if (ctrl && event.key === Qt.Key_A) {
                grid.select(grid.ordered().map(tile => tile.key))
            } else if (ctrl && event.key === Qt.Key_C) {
                const files = grid.selectedTiles().filter(tile => tile.kind !== "launcher")
                if (files.length === 0)
                    return
                grid.model.copy(files.map(tile => tile.key))
                for (const tile of files)
                    tile.hop()
            } else if (plain && event.text.length > 0 && event.text.trim() !== "" && event.text >= " ") {
                grid.typeAhead(event.text)
            } else {
                return
            }
        }
        event.accepted = true
    }

    function open(list) {
        for (const tile of list) {
            tile.boing()
            if (tile.kind === "launcher")
                shell.launch(tile.appId)
            else
                model.open(tile.key)
        }
    }
    // Shrinks the icons away, then moves their files to the trash.
    function trash(list) {
        const keys = list.filter(tile => tile.kind !== "launcher").map(tile => tile.key)
        for (const tile of list)
            if (tile.kind !== "launcher")
                tile.leaving = true
        trashing.keys = keys
        trashing.restart()
    }
    Timer {
        id: trashing
        property var keys: []
        interval: Theme.duration(170)
        onTriggered: {
            const tiles = grid.tiles()
            if (grid.model.trash(keys) < keys.length)
                for (const tile of tiles)
                    tile.leaving = false
        }
    }
    function tidy() {
        tidying = true
        settleGlide.restart()
        model.tidy()
    }

    // The icon whose name is being typed, "" while none is, and what its field holds.
    property string renaming: ""
    property string renameText: ""
    // A name Enter gave was refused: the field stays, and shakes.
    signal renameRefused()
    // Turns the name of the file or folder of `key` into a field, holding `text` (its name unless
    // given), which takes the keyboard. Launchers' names are the configuration's.
    function startRename(key, text) {
        const tile = tileOf(key)
        if (!tile || !shown || (tile.kind !== "file" && tile.kind !== "folder"))
            return false
        select([key])
        current = extendFrom = key
        renameText = text === undefined ? tile.name : text
        renaming = key
        return true
    }
    // Ends the renaming: with `text` through the model, or as it was without (`text` undefined,
    // as Escape ends it). A name the model refuses (saying why across the panel) keeps the field on
    // it with `keep`, as Enter does; the field losing the keyboard, or a press elsewhere, lets it go
    // as file managers do.
    function finishRename(text, keep) {
        const key = renaming
        if (key === "")
            return
        renaming = ""
        forceActiveFocus()
        const tile = tileOf(key)
        if (text === undefined || !tile || text === tile.name)
            return
        const renamed = model.rename(key, text)
        if (renamed !== "") {
            select([renamed])
            current = extendFrom = renamed
        } else if (keep && startRename(key, text)) {
            renameRefused()
        }
    }
    Connections {
        target: grid
        function onPressed() { grid.finishRename(grid.renameText, false) }
    }
    // A new folder in the cell under `x`, `y` (or the free one nearest it), popping in with a puff
    // of sparkles, its name to type.
    function newFolder(x, y) {
        const column = Math.max(0, Math.min(columns - 1, Math.floor((x - area.x - margin) / cellWidth)))
        const row = Math.max(0, Math.min(rows - 1, Math.floor((y - area.y - margin) / cellHeight)))
        rippleOrigin = Qt.point(x, y)
        const tile = tileOf(model.newFolder(column, row))
        if (!tile)
            return
        if (Theme.duration(700) > 0)
            puff.createObject(grid, { x: tile.x + tile.width / 2, y: tile.y + Theme.spacingM + iconSize / 2 })
        startRename(tile.key)
    }

    // The drag of the selected icons: `anchorKey` under the pointer, the others in `dragKeys`
    // after it; where the pointer went down and is now; and where they would land.
    property bool dragging: false
    // Whether a drag of files is the compositor's (a drag of Wayland's, out to any window): on
    // Qt's Wayland platform. Elsewhere, as on the tests' offscreen one, the icons follow the
    // pointer over the desktop alone.
    readonly property bool handsOver: Qt.platform.pluginName === "wayland"
    // The drag handed over: the icons faded in their cells while their stack's picture is taken,
    // and the compositor has it (`carried`) until it ends (`model.dragEnded`); whether it is over
    // something other than the desktop (`away`), and whether the icons are coming out of the stack
    // where it was let go (`snapping`).
    property bool handedOver: false
    property bool carried: false
    property bool away: false
    property bool snapping: false
    property var dragKeys: []
    property int anchorColumn: 0
    property int anchorRow: 0
    property point anchorHome
    property point dragStart
    property point dragPointer
    property var landing: []
    readonly property real dragX: anchorHome.x + dragPointer.x - dragStart.x
    readonly property real dragY: anchorHome.y + dragPointer.y - dragStart.y
    readonly property int dragColumns: Math.round((dragX - area.x - margin) / cellWidth) - anchorColumn
    readonly property int dragRows: Math.round((dragY - area.y - margin) / cellHeight) - anchorRow
    onDragColumnsChanged: updateLanding()
    onDragRowsChanged: updateLanding()
    function updateLanding() {
        landing = dragging && !away ? model.landing(dragKeys, dragColumns, dragRows) : []
    }
    onAwayChanged: updateLanding()
    function beginDrag(tile, at) {
        dragKeys = [tile.key].concat(selectionOrder.filter(key => key !== tile.key))
        anchorColumn = tile.column
        anchorRow = tile.row
        anchorHome = Qt.point(cellX(tile.column), cellY(tile.row))
        dragStart = at
        dragPointer = at
        gliding = false
        tidying = false
        away = false
        // Launchers alone stay on the desktop.
        handedOver = handsOver && dragKeys.some(key => { const t = tileOf(key); return t && t.kind !== "launcher" })
        dragging = true
        updateLanding()
        if (handedOver) {
            dragStack.tiles = dragKeys.map(key => tileOf(key)).filter(t => !!t)
            dragStack.wanted = true
            dragStack.take()
        }
    }
    function dragTo(at) { dragPointer = at }
    // The stack's picture is taken: the compositor carries it, the pointer at the place on it of
    // where the icon was pressed. A drag it never got (the button let go meanwhile, or nothing to
    // hand over) goes on as the pointer's.
    function carry(image, scale) {
        if (!dragging || !handedOver)
            return
        const hotSpot = Qt.point(dragStack.origin.x + dragStart.x - anchorHome.x,
                                 dragStack.origin.y + dragStart.y - anchorHome.y)
        carried = model.drag(dragKeys, image, hotSpot, scale)
        handedOver = carried
    }
    Connections {
        target: grid.model
        // Dropped on another window, or let go nowhere: the icons fade back in in their cells.
        function onDragEnded(action) {
            grid.carried = false
            if (grid.dragging && grid.handedOver)
                grid.endDrag(true)
        }
    }
    function endDrag(cancelled) {
        // Let go on the desktop, a stack the compositor carried comes out where it was let go,
        // its icons gliding from there.
        if (handedOver && !cancelled) {
            snapping = true
            handedOver = false
            snapping = false
        }
        handedOver = false
        dragStack.wanted = false
        gliding = true
        settleGlide.restart()
        if (!cancelled)
            model.move(dragKeys, dragColumns, dragRows)
        dragging = false
        away = false
        landing = []
    }
    // Whether the icons are gliding into cells they were dropped on or tidied into, rather than
    // trailing the pointer.
    property bool gliding: false
    property bool tidying: false
    Timer {
        id: settleGlide
        interval: Theme.duration(1200) + 50
        onTriggered: { grid.gliding = false; grid.tidying = false }
    }
    // How long an icon takes to its place: trailing the pointer, each further back in the stack a
    // little slower; dropped, the further back the later; tidied, in a wave from the top left.
    function glideDuration(tile) {
        if (tile.dragged)
            return Theme.duration(tile.rank === 0 ? 0 : 60 + 45 * Math.min(tile.rank, 6))
        if (tidying)
            return Theme.duration(320 + 22 * Math.min(tile.column + tile.row, 24))
        return Theme.duration(300 + 45 * Math.min(dragKeys.indexOf(tile.key) + 1, 8))
    }

    // The band a drag across the desktop draws, from where it started to the pointer.
    property bool banding: false
    property point bandFrom
    property point bandTo
    readonly property rect band: Qt.rect(Math.min(bandFrom.x, bandTo.x), Math.min(bandFrom.y, bandTo.y),
                                         Math.abs(bandTo.x - bandFrom.x), Math.abs(bandTo.y - bandFrom.y))
    // What was selected as the band began: kept, with Ctrl or Shift held.
    property var bandBase: []
    function bandSelect() {
        const inside = []
        for (const tile of tiles()) {
            if (!tile.visible)
                continue
            // The icon and its name, not the whole cell.
            const left = tile.x + Theme.spacingM, top = tile.y + Theme.spacingS
            const right = tile.x + tile.width - Theme.spacingM, bottom = tile.y + tile.height - Theme.spacingS
            if (left < band.x + band.width && right > band.x && top < band.y + band.height && bottom > band.y)
                inside.push(tile.key)
        }
        // Those caught before stay first, in their order, after those it began with.
        const base = bandBase
        const kept = selectionOrder.filter(key => inside.indexOf(key) >= 0 && base.indexOf(key) < 0)
        select(base.concat(kept, inside.filter(key => kept.indexOf(key) < 0 && base.indexOf(key) < 0)))
    }

    // The ripple: from where the menu was opened, an icon further away coming or going later.
    property point rippleOrigin: Qt.point(area.x, area.y)
    function rippleDelay(tile) {
        const dx = tile.x + tile.width / 2 - rippleOrigin.x, dy = tile.y + tile.height / 2 - rippleOrigin.y
        return Theme.duration(Math.min(480, Math.hypot(dx, dy) * 0.45))
    }
    // Whether the icons first laid out have come: an icon after that is a new file, and pops in.
    property bool settled: false
    Timer { running: true; interval: 800; onTriggered: grid.settled = true }

    // Presses on the desktop beside the icons: the band, or the desktop's menu.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            grid.pressed()
            grid.keysUsed = false
            const adding = mouse.button === Qt.LeftButton &&
                           (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
            grid.bandBase = adding ? grid.selectionOrder.slice() : []
            if (!adding)
                grid.select([])
            if (mouse.button === Qt.RightButton) {
                grid.menuRequested(mouse.x, mouse.y, false)
                return
            }
            grid.bandFrom = grid.bandTo = Qt.point(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (!(pressedButtons & Qt.LeftButton))
                return
            grid.bandTo = Qt.point(mouse.x, mouse.y)
            if (!grid.banding && grid.shown &&
                Math.hypot(grid.bandTo.x - grid.bandFrom.x, grid.bandTo.y - grid.bandFrom.y) >= Qt.styleHints.startDragDistance)
                grid.banding = true
            if (grid.banding)
                grid.bandSelect()
        }
        onReleased: grid.banding = false
        onCanceled: grid.banding = false
    }

    // The drag the compositor carries, over the desktop: the pointer moving the pads, and let go
    // here, the icons landing as from the pointer's own drag.
    DropArea {
        id: dropArea
        anchors.fill: parent
        enabled: grid.shown
        function follow(drag) {
            if (drag.source !== grid.model || !grid.dragging || !grid.handedOver) {
                drag.accepted = false
                return
            }
            grid.away = false
            grid.dragTo(Qt.point(drag.x, drag.y))
            drag.accept(Qt.MoveAction)
        }
        onEntered: function(drag) { follow(drag) }
        onPositionChanged: function(drag) { follow(drag) }
        onExited: if (grid.handedOver) grid.away = true
        onDropped: function(drop) {
            if (drop.source !== grid.model || !grid.dragging || !grid.handedOver)
                return
            grid.dragTo(Qt.point(drop.x, drop.y))
            grid.endDrag(false)
            drop.accept(Qt.MoveAction)
        }
    }

    // The picture of the dragged stack the compositor carries.
    DesktopDragStack {
        id: dragStack
        board: grid
        // Taken once every picture in it has loaded.
        property bool wanted: false
        onReadyChanged: take()
        function take() {
            if (!wanted || !ready)
                return
            wanted = false
            const scale = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
            if (!grabToImage(function(result) { grid.carry(result.image, scale) },
                             Qt.size(Math.ceil(width * scale), Math.ceil(height * scale))))
                grid.handedOver = false
        }
    }

    // The cells the dragged icons will land in.
    Repeater {
        model: grid.landing
        delegate: Shape {
            id: pad
            required property var modelData
            objectName: "desktopLanding"
            x: grid.cellX(modelData.column) + Theme.spacingXS
            y: grid.cellY(modelData.row) + Theme.spacingXS
            width: grid.cellWidth - 2 * Theme.spacingXS
            height: grid.cellHeight - 2 * Theme.spacingXS
            Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack } }
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack } }
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.alpha(Theme.accent, 0.85)
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: Theme.alpha(Theme.accent, 0.12)
                PathRectangle { width: pad.width; height: pad.height; radius: Theme.radiusLarge }
            }
        }
    }

    Repeater {
        id: icons
        model: grid.active ? grid.model : null
        delegate: DesktopIcon { board: grid }
    }

    // The ring of the current icon while the keys move it: in the second hue with a soft halo,
    // hopping from icon to icon, a sparkle on its corner twinkling as it lands; over it, what was
    // typed to find it.
    Item {
        id: ring
        objectName: "desktopFocusRing"
        readonly property Item tile: grid.currentTile
        z: 3
        width: grid.cellWidth
        height: grid.cellHeight
        visible: opacity > 0
        opacity: grid.ringShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        // Hidden, it follows the current icon at once, and comes in where that is; shown, it glides.
        property bool landed: false
        onVisibleChanged: {
            if (!visible) {
                landed = false
                return
            }
            starPop.restart()
            Qt.callLater(function() { ring.landed = ring.visible })
        }
        x: tile ? tile.x : 0
        y: tile ? tile.y : 0
        Behavior on x { enabled: ring.landed; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
        Behavior on y { enabled: ring.landed; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
        onTileChanged: if (visible) starPop.restart()
        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.spacingXS - 3
            radius: Theme.radiusLarge + 3
            color: "transparent"
            border.width: 3
            border.color: Theme.alpha(grid.partner, 0.3)
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.spacingXS
            radius: Theme.radiusLarge
            color: "transparent"
            border.width: Theme.focusRingWidth
            border.color: grid.partner
        }
        Rectangle {
            id: star
            width: 20
            height: 20
            radius: 10
            x: parent.width - Theme.spacingXS - width / 2 - 3
            y: Theme.spacingXS - height / 2 + 3
            color: Theme.surface
            border.color: grid.partner
            border.width: 1.5
            Icon {
                anchors.centerIn: parent
                name: "sparkles"
                size: 13
                color: grid.partner
            }
            SequentialAnimation {
                id: starPop
                ParallelAnimation {
                    NumberAnimation { target: star; property: "scale"; from: 0.3; to: 1.25; duration: Theme.duration(140); easing.type: Easing.OutQuad }
                    NumberAnimation { target: star; property: "rotation"; from: -60; to: 8; duration: Theme.duration(140); easing.type: Easing.OutQuad }
                }
                ParallelAnimation {
                    NumberAnimation { target: star; property: "scale"; to: 1; duration: Theme.duration(220); easing.type: Easing.OutBack }
                    NumberAnimation { target: star; property: "rotation"; to: 0; duration: Theme.duration(220); easing.type: Easing.OutBack }
                }
            }
        }
        Rectangle {
            objectName: "desktopTypeAhead"
            visible: grid.typed !== ""
            height: 20
            width: Math.max(height, typedText.implicitWidth + 2 * Theme.spacingM)
            radius: height / 2
            x: Math.round((parent.width - width) / 2)
            y: Theme.spacingXS - height / 2
            color: grid.partner
            border.color: Theme.surface
            border.width: 2
            Text {
                id: typedText
                anchors.centerIn: parent
                text: grid.typed
                color: Theme.contrast(grid.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
            }
        }
    }

    // A puff out of an icon that has just come, a new folder: a ring in the accent's partner
    // spreading out, dots in both hues and four sparkles flying out and fading, gone once they
    // have.
    Component {
        id: puff
        Item {
            id: burst
            z: 120
            property real progress: 0
            Rectangle {
                width: grid.iconSize * (0.5 + 1.2 * burst.progress)
                height: width
                x: -width / 2
                y: -height / 2
                radius: width / 2
                color: "transparent"
                border.width: 0.5 + 2.5 * (1 - burst.progress)
                border.color: Theme.alpha(grid.partner, 0.8 * (1 - burst.progress))
            }
            Repeater {
                model: 8
                delegate: Rectangle {
                    required property int index
                    readonly property real angle: (index + 0.5) * Math.PI / 4
                    readonly property real reach: grid.iconSize * (index % 2 ? 0.7 : 1) * burst.progress
                    width: index % 2 ? 6 : 8
                    height: width
                    radius: width / 2
                    color: index % 2 ? grid.partner : Theme.accent
                    x: Math.cos(angle) * reach - width / 2
                    y: Math.sin(angle) * reach - height / 2
                    opacity: 1 - burst.progress * burst.progress
                    scale: 1 - 0.5 * burst.progress
                }
            }
            Repeater {
                model: [[-0.8, -0.85, 15], [0.9, -0.6, 12], [-0.95, 0.4, 10], [0.75, 0.8, 11]]
                delegate: Icon {
                    required property var modelData
                    required property int index
                    name: "sparkles"
                    size: modelData[2]
                    color: index % 2 ? Theme.accent : grid.partner
                    x: modelData[0] * grid.iconSize * burst.progress - size / 2
                    y: modelData[1] * grid.iconSize * burst.progress - size / 2
                    rotation: 90 * burst.progress
                    scale: 0.6 + 0.6 * Math.sin(Math.PI * Math.min(1, burst.progress * 1.4))
                    opacity: 1 - burst.progress * burst.progress
                }
            }
            NumberAnimation on progress {
                to: 1
                duration: Theme.duration(700)
                easing.type: Easing.OutCubic
                onFinished: burst.destroy()
            }
        }
    }

    // The band, over the icons it catches.
    Item {
        objectName: "desktopSelectionBand"
        visible: grid.banding
        x: grid.band.x; y: grid.band.y
        width: grid.band.width; height: grid.band.height
        Rectangle {
            anchors.fill: parent
            radius: Math.min(Theme.radiusMedium, width / 2, height / 2)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.22) }
                GradientStop { position: 1; color: Theme.alpha(grid.partner, 0.22) }
            }
            border.width: 1.5
            border.color: Theme.alpha(Theme.mix(Theme.accent, grid.partner, 0.35), 0.8)
        }
        // The count, at the pointer's corner.
        Rectangle {
            id: bubble
            visible: grid.selectionOrder.length > 0
            height: 22
            width: Math.max(height, tally.implicitWidth + twinkle.width + 3 * Theme.spacingS)
            radius: height / 2
            x: (grid.bandTo.x >= grid.bandFrom.x ? parent.width : 0) - width / 2
            y: (grid.bandTo.y >= grid.bandFrom.y ? parent.height : 0) - height / 2
            color: grid.partner
            border.color: Theme.surface
            border.width: 2
            Icon {
                id: twinkle
                name: "sparkles"
                size: 11
                color: tally.color
                x: Theme.spacingS + 1
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: tally
                x: twinkle.x + twinkle.width + Theme.spacingXS
                anchors.verticalCenter: parent.verticalCenter
                text: grid.selectionOrder.length
                color: Theme.contrast(grid.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
            }
            // A hop at each icon caught or let go.
            scale: 1
            SequentialAnimation on scale {
                id: bump
                running: false
                NumberAnimation { to: 1.18; duration: Theme.duration(80); easing.type: Easing.OutQuad }
                NumberAnimation { to: 1; duration: Theme.duration(180); easing.type: Easing.OutBack }
            }
            Connections {
                target: grid
                function onSelectionOrderChanged() { if (grid.banding) bump.restart() }
            }
        }
    }
}
