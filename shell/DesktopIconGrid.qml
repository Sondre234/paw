// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// The desktop's icons (shell.desktop_icons, the model shell.desktopIcons) on a grid in `area`, the
// room the bars leave, on the main monitor (`active`), and the presses on the desktop around them.
// A click selects an icon, the middle button picks one more or puts one back, and a drag across
// the desktop selects every icon its band touches: a band in the accent colour fading into a
// second hue, with rounded corners and a bubble at the pointer counting what it holds, each icon
// it catches hopping as it does. Dragged, the selected icons gather under the pointer into a stack
// with their count, while dashed pads on the grid show the cells they will land in, gliding
// from cell to cell; let go, they glide into those cells, the furthest last. Shown and hidden from
// the desktop's menu, the icons ripple out from where it was opened.
Item {
    id: grid
    objectName: "desktopIconGrid"
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
    signal menuRequested(real x, real y)
    signal iconMenuRequested(real x, real y)

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
    // An icon gone from the folder leaves the selection.
    Connections {
        target: grid.model
        function onCountChanged() {
            const kept = grid.selectedTiles().map(tile => tile.key)
            if (kept.length !== grid.selectionOrder.length)
                grid.select(kept)
        }
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

    // The drag of the selected icons: `anchorKey` under the pointer, the others in `dragKeys`
    // after it; where the pointer went down and is now; and where they would land.
    property bool dragging: false
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
    function updateLanding() { landing = dragging ? model.landing(dragKeys, dragColumns, dragRows) : [] }
    function beginDrag(tile, at) {
        dragKeys = [tile.key].concat(selectionOrder.filter(key => key !== tile.key))
        anchorColumn = tile.column
        anchorRow = tile.row
        anchorHome = Qt.point(cellX(tile.column), cellY(tile.row))
        dragStart = at
        dragPointer = at
        gliding = false
        tidying = false
        dragging = true
        updateLanding()
    }
    function dragTo(at) { dragPointer = at }
    function endDrag(cancelled) {
        gliding = true
        settleGlide.restart()
        if (!cancelled)
            model.move(dragKeys, dragColumns, dragRows)
        dragging = false
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
        // Those caught before stay first, in their order.
        const kept = selectionOrder.filter(key => inside.indexOf(key) >= 0)
        select(kept.concat(inside.filter(key => kept.indexOf(key) < 0)))
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
            grid.select([])
            if (mouse.button === Qt.RightButton) {
                grid.menuRequested(mouse.x, mouse.y)
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
