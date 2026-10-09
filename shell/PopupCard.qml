// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// A popup's card: Theme's opaque surface with an outline and rounded corners, and a shadow under
// it when the GPU draws (Theme.effects). Setting `open` fades it in with a few pixels' slide from
// the side it opens from; clearing it fades it out the same way, and it stays visible until it
// has. Both are instant with animations off. What it holds goes inside it, filling it. As it
// opens it emits opened(), where what it shows is reset, and `initialFocus` (the card itself
// unless set; null for none) takes the keyboard. Presses on it stay with it, so that they do not
// close it, until it starts closing: a press then goes to what is under it.
//
// It places itself beside `anchorRect`, a rectangle in its parent's coordinates (a bar item's,
// from panel.barAnchor): on the anchor's `side` (Qt.TopEdge above it, Qt.BottomEdge below it,
// Qt.LeftEdge or Qt.RightEdge beside it), `gap` away, on the other side instead when that one has
// more room for it, and at least `margin` inside `bounds` (its parent's rectangle, unless set; a
// bar's popup keeps off the bar with panel.popupArea). `alignment` lines it up with the
// anchor along that side: Qt.AlignHCenter or Qt.AlignVCenter centres it, Qt.AlignLeft or
// Qt.AlignTop starts it at the anchor's left or top edge, Qt.AlignRight or Qt.AlignBottom ends it
// at the other. Its size is its implicit size, cut down to the room there is (availableWidth and
// availableHeight), so set implicitWidth and implicitHeight rather than width and height. With
// `glides`, a card that is shown and given another place or size (its anchor moved, what it holds
// grew) eases there, rather than jumping; one that is not shown takes it at once, so that opening
// and closing keep their own motion. With `anchored` false it leaves its place to whoever uses
// it. With `framed` false it draws no card of its own, for what it holds to draw its own cards
// (the macOS style's Notification Center); a press between them then goes to what is under it.
Item {
    id: card
    property bool open: false
    property rect anchorRect
    property int side: Qt.TopEdge
    property int alignment: Qt.AlignHCenter
    property real gap: Theme.spacingM
    property real margin: Theme.spacingM
    property bool anchored: true
    property bool framed: true
    property bool glides: false
    property color color: Theme.popupSurface
    property real radius: Theme.radiusMedium
    property Item initialFocus: card
    default property alias content: body.data
    // How far it is open, from 0 to 1: its opacity, and what is left of the slide.
    property real progress: 0

    property rect bounds: parent ? Qt.rect(0, 0, parent.width, parent.height) : Qt.rect(0, 0, 0, 0)

    readonly property bool vertical: side === Qt.TopEdge || side === Qt.BottomEdge
    // The room on each side of the anchor.
    readonly property real roomAbove: anchorRect.y - gap - margin - bounds.y
    readonly property real roomBelow: bounds.y + bounds.height - anchorRect.y - anchorRect.height - gap - margin
    readonly property real roomLeft: anchorRect.x - gap - margin - bounds.x
    readonly property real roomRight: bounds.x + bounds.width - anchorRect.x - anchorRect.width - gap - margin
    function room(edge) {
        return edge === Qt.TopEdge ? roomAbove : edge === Qt.BottomEdge ? roomBelow
             : edge === Qt.LeftEdge ? roomLeft : roomRight
    }
    function opposite(edge) {
        return edge === Qt.TopEdge ? Qt.BottomEdge : edge === Qt.BottomEdge ? Qt.TopEdge
             : edge === Qt.LeftEdge ? Qt.RightEdge : Qt.LeftEdge
    }
    // The side it opens on: `side`, unless it does not fit there and the other side has more room.
    readonly property int placedSide: {
        var wanted = vertical ? implicitHeight : implicitWidth
        return wanted > room(side) && room(opposite(side)) > room(side) ? opposite(side) : side
    }
    readonly property real availableWidth: vertical ? bounds.width - 2 * margin : room(placedSide)
    readonly property real availableHeight: vertical ? room(placedSide) : bounds.height - 2 * margin
    // Its place and size, which it has once done gliding: its place follows from this size, so
    // that it heads straight for where it ends.
    readonly property real placedWidth: Math.max(0, Math.min(implicitWidth, availableWidth))
    readonly property real placedHeight: Math.max(0, Math.min(implicitHeight, availableHeight))
    readonly property real placedX: vertical
        ? along(anchorRect.x, anchorRect.width, placedWidth, bounds.x, bounds.x + bounds.width)
        : placedSide === Qt.LeftEdge ? anchorRect.x - gap - placedWidth
        : anchorRect.x + anchorRect.width + gap
    readonly property real placedY: !vertical
        ? along(anchorRect.y, anchorRect.height, placedHeight, bounds.y, bounds.y + bounds.height)
        : placedSide === Qt.TopEdge ? anchorRect.y - gap - placedHeight
        : anchorRect.y + anchorRect.height + gap
    width: placedWidth
    height: placedHeight

    // Where alignment puts it along the anchor, from `start` (the anchor's start) and `length`
    // (its length) and the card's own `size`, kept inside the bounds from `low` to `high`.
    function along(start, length, size, low, high) {
        var at = alignment & (Qt.AlignLeft | Qt.AlignTop) ? start
               : alignment & (Qt.AlignRight | Qt.AlignBottom) ? start + length - size
               : start + length / 2 - size / 2
        return Math.max(low + margin, Math.min(at, high - margin - size))
    }
    // Its place on the way there, which a Behavior can ease (what Binding writes, none can).
    property real glideX: placedX
    property real glideY: placedY
    Binding {
        when: card.anchored
        card.x: card.glideX
        card.y: card.glideY
    }
    // Place and size ease together, so that an edge that stays (the one by the anchor) stays.
    Behavior on glideX {
        enabled: card.glides && card.visible
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    Behavior on glideY {
        enabled: card.glides && card.visible
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    Behavior on width {
        enabled: card.glides && card.visible
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    Behavior on height {
        enabled: card.glides && card.visible
        NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
    }

    visible: open || progress > 0
    opacity: progress
    // Once each time it opens, when it is visible: made open, opened, or opened again while it
    // was still fading out.
    signal opened()
    onOpened: if (initialFocus) initialFocus.forceActiveFocus()
    property bool announced: false
    function announce() {
        if (open && visible && !announced) {
            announced = true
            opened()
        }
    }
    onOpenChanged: {
        if (!open) announced = false
        announce()
    }
    onVisibleChanged: announce()
    Component.onCompleted: announce()
    states: State {
        name: "open"
        when: card.open
        PropertyChanges { card.progress: 1 }
    }
    transitions: [
        Transition {
            to: "open"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "open"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]
    // It slides out from the anchor, a few pixels.
    transform: Translate {
        readonly property real distance: (1 - card.progress) * Theme.spacingM
        x: card.placedSide === Qt.LeftEdge ? distance : card.placedSide === Qt.RightEdge ? -distance : 0
        y: card.placedSide === Qt.TopEdge ? distance : card.placedSide === Qt.BottomEdge ? -distance : 0
    }

    Loader {
        anchors.fill: parent
        active: Theme.effects && card.framed
        sourceComponent: RectangularShadow {
            radius: card.radius
            blur: Theme.shadowBlur
            offset: Qt.vector2d(0, Theme.shadowOffset)
            color: Theme.shadow
        }
    }
    Rectangle {
        visible: card.framed
        anchors.fill: parent
        color: card.color
        radius: card.radius
        border.color: Theme.popupOutline
        Rectangle {
            visible: Theme.popupInnerEdge.a > 0 && card.color.a > 0
            anchors.fill: parent; anchors.margins: 1
            radius: card.radius - 1
            color: "transparent"
            border.color: Theme.popupInnerEdge
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: card.open && card.framed
        acceptedButtons: Qt.AllButtons
    }
    Item {
        id: body
        anchors.fill: parent
        enabled: card.open
    }
}
