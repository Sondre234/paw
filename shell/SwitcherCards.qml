// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects
import Paw

// The window switcher's windows as cards, as Windows 11 lists them on Alt+Tab: each window's
// application icon and title over a picture of it, the pictures at a common height and each as
// wide as its window's proportions make it, in rows that wrap and are centred. The rows fit in
// `maxWidth` by `maxHeight`, the pictures shrinking down to `minimumScale` of their height when
// there are many windows, and past that the rows scroll to the selection. The selected card is
// marked as the icon grid marks its cell, the mark gliding from card to card while `animate`.
//
// A card finds its window's task in `taskSource` by the number the switcher gives the window
// (the task model's windowId role) and, while `watching`, asks the model for its picture with
// watchPicture; until a picture has come, or without one, the application's icon stands in, and
// the first fades in over it. A model without watchPicture (a preview's) names pictures of its
// own.
Item {
    id: cards
    // The switcher's windows ({appId, title, output, workspace, minimized, urgent, id}), the
    // selected one, and the task model their pictures come from.
    required property var windows
    required property int selected
    required property var taskSource
    // Whether the switcher is open, so that its windows' pictures are wanted.
    required property bool watching
    required property real maxWidth
    required property real maxHeight
    required property bool animate
    objectName: "switcherCards"
    // A picture's height at full size, that of the box the taskbar's pictures fit in; a card's
    // picture is as wide as its window's proportions make it, from 3:4 to 2:1, 16:10 while it has
    // none.
    readonly property int fullHeight: Math.round(shell.thumbnailSize * 0.625)
    readonly property real minimumScale: 0.6
    readonly property real narrowestAspect: 0.75
    readonly property real widestAspect: 2
    readonly property real unknownAspect: 1.6
    // Inside a card, around its header and picture; between cards; the header's height.
    readonly property int padding: Theme.spacingM
    readonly property int gap: Theme.spacingM
    readonly property int headerHeight: Theme.iconSize + Theme.spacingS
    // How wide and tall each window's picture is, by window number, once one has shown.
    property var aspects: ({})
    function noteAspect(id, aspect) {
        if (id <= 0 || !(aspect > 0) || Math.abs((aspects[id] || 0) - aspect) < 0.01)
            return
        var next = Object.assign({}, aspects)
        next[id] = aspect
        aspects = next
    }
    // The cards' places: {scale, pictureHeight, cardHeight, places: [{x, y, width}], width,
    // height}, for the largest scale whose rows fit, or the smallest.
    function arrange(windows, aspects, maxWidth, maxHeight) {
        var result = null
        for (var step = 0; ; ++step) {
            var scale = Math.max(minimumScale, 1 - 0.05 * step)
            var pictureHeight = Math.round(fullHeight * scale)
            var cardHeight = 2 * padding + headerHeight + Theme.spacingS + pictureHeight
            var rows = [], row = [], rowWidth = 0
            for (var i = 0; i < windows.length; ++i) {
                var aspect = Math.max(narrowestAspect,
                                      Math.min(widestAspect, aspects[windows[i].id] || unknownAspect))
                var width = Math.round(pictureHeight * aspect) + 2 * padding
                if (row.length > 0 && rowWidth + gap + width > maxWidth) {
                    rows.push({cards: row, width: rowWidth})
                    row = []
                    rowWidth = 0
                }
                rowWidth += (row.length > 0 ? gap : 0) + width
                row.push(width)
            }
            if (row.length > 0)
                rows.push({cards: row, width: rowWidth})
            var widestRow = 0
            for (var r = 0; r < rows.length; ++r)
                widestRow = Math.max(widestRow, rows[r].width)
            var places = []
            for (r = 0; r < rows.length; ++r) {
                var x = Math.round((widestRow - rows[r].width) / 2)
                for (var c = 0; c < rows[r].cards.length; ++c) {
                    places.push({x: x, y: r * (cardHeight + gap), width: rows[r].cards[c]})
                    x += rows[r].cards[c] + gap
                }
            }
            result = {scale: scale, pictureHeight: pictureHeight, cardHeight: cardHeight,
                      places: places, width: widestRow,
                      height: rows.length > 0 ? rows.length * (cardHeight + gap) - gap : 0}
            if (result.height <= maxHeight || scale <= minimumScale)
                return result
        }
    }
    readonly property var layout: arrange(windows, aspects, maxWidth, maxHeight)
    implicitWidth: layout.width
    implicitHeight: Math.min(layout.height, maxHeight)
    readonly property var current: layout.places[selected] || null

    Flickable {
        id: view
        objectName: "switcherCardsView"
        anchors.fill: parent
        contentWidth: cards.layout.width; contentHeight: cards.layout.height
        interactive: false
        clip: contentHeight > height
        // Scrolled only as far as keeps the selected card in sight.
        function follow() {
            var place = cards.current
            if (!place)
                return
            var top = contentY
            if (place.y < top)
                top = place.y
            else if (place.y + cards.layout.cardHeight > top + height)
                top = place.y + cards.layout.cardHeight - height
            top = Math.max(0, Math.min(top, contentHeight - height))
            if (top !== contentY)
                contentY = top
        }
        Behavior on contentY { enabled: cards.animate; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Connections {
            target: cards
            function onCurrentChanged() { view.follow() }
            function onLayoutChanged() { view.follow() }
        }
        onHeightChanged: follow()

        // The selection, under the cards.
        Rectangle {
            objectName: "switcherSelection"
            visible: cards.current !== null
            x: cards.current ? cards.current.x : 0
            y: cards.current ? cards.current.y : 0
            width: cards.current ? cards.current.width : 0
            height: cards.layout.cardHeight
            radius: Theme.radiusMedium
            color: Theme.switcherSelection
            border.color: Theme.accent; border.width: 2
            Behavior on x { enabled: cards.animate; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on y { enabled: cards.animate; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on width { enabled: cards.animate; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        }

        Repeater {
            model: cards.windows
            delegate: Item {
                id: card
                required property var modelData
                required property int index
                objectName: "switcherCard"
                readonly property var place: cards.layout.places[index] || ({x: 0, y: 0, width: 0})
                x: place.x; y: place.y
                width: place.width; height: cards.layout.cardHeight
                readonly property bool current: index === cards.selected
                readonly property bool minimized: modelData.minimized === true
                readonly property bool urgent: modelData.urgent === true
                readonly property int windowId: modelData.id || 0
                readonly property string icon: "image://icons/" + shell.iconFor(modelData.appId)

                // Its window's task, found by the window's number; none without one.
                TaskFilter {
                    id: task
                    sourceModel: card.windowId > 0 ? cards.taskSource : null
                    windowId: card.windowId
                }
                readonly property var window: task.count > 0 ? task.windows[0] : null
                readonly property int taskId: window ? window.taskId : -1
                // The picture is watched while the switcher is open, from the model it was asked
                // of, should the switcher's change in between.
                readonly property bool wanted: cards.watching && taskId >= 0 && !!cards.taskSource &&
                                               typeof cards.taskSource.watchPicture === "function"
                readonly property bool live: shell.liveThumbnails
                readonly property int pixelWidth: Math.round(cards.widestAspect * cards.fullHeight * Screen.devicePixelRatio)
                property var watchedModel: null
                property int watchedTask: -1
                property bool watchedLive: false
                property int watchedWidth: 0
                function rewatch() {
                    if (wanted && (watchedModel !== cards.taskSource || watchedTask !== taskId ||
                                   watchedLive !== live || watchedWidth !== pixelWidth)) {
                        // Watched again before the last watch goes, so that the count never
                        // reaches 0 in between and the capture goes on with the new liveness.
                        cards.taskSource.watchPicture(taskId, pixelWidth, live)
                        if (watchedModel)
                            watchedModel.unwatchPicture(watchedTask)
                        watchedModel = cards.taskSource
                        watchedTask = taskId
                        watchedLive = live
                        watchedWidth = pixelWidth
                    } else if (!wanted && watchedModel) {
                        watchedModel.unwatchPicture(watchedTask)
                        watchedModel = null
                        watchedTask = -1
                    }
                }
                onWantedChanged: rewatch()
                onTaskIdChanged: rewatch()
                onLiveChanged: rewatch()
                onPixelWidthChanged: rewatch()
                Component.onCompleted: rewatch()
                Component.onDestruction: {
                    if (watchedModel)
                        watchedModel.unwatchPicture(watchedTask)
                }

                // A window asking for attention is tinted in the urgent colour, but for the
                // selection's own.
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMedium
                    color: card.urgent && !card.current ? Theme.urgentSubtle : "transparent"
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: pick.containsMouse ? Theme.hover : "transparent"
                    }
                }
                // The application's icon and the window's title.
                Item {
                    id: header
                    x: cards.padding; y: cards.padding
                    width: parent.width - 2 * cards.padding; height: cards.headerHeight
                    Image {
                        id: headerIcon
                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.spacingXS
                        width: Theme.iconSizeSmall; height: width
                        sourceSize: Qt.size(2 * width, 2 * height)
                        source: card.icon
                        opacity: card.minimized ? 0.45 : 1
                    }
                    Text {
                        objectName: "switcherCardTitle"
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: headerIcon.right; anchors.leftMargin: Theme.spacingS
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacingXS
                        text: card.modelData.title.length > 0 ? card.modelData.title : card.modelData.appId
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: card.minimized ? Theme.textMuted : Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                    }
                }
                // The picture, as large as fits in its box, keeping its proportions; with the
                // GPU its corners are rounded as the box's. A minimized window's is faded.
                Item {
                    id: box
                    x: cards.padding; y: header.y + header.height + Theme.spacingS
                    width: parent.width - 2 * cards.padding; height: cards.layout.pictureHeight
                    Rectangle {
                        objectName: "switcherCardStandIn"
                        anchors.fill: parent
                        // Fading out under the first picture as it fades in.
                        opacity: picture.pictured ? 1 - picture.reveal : 1
                        visible: opacity > 0
                        radius: Theme.radiusSmall
                        color: Theme.alpha(Theme.text, 0.06)
                        Image {
                            anchors.centerIn: parent
                            width: Math.min(Theme.appIconSizeDisplay, Math.round(parent.height * 0.5)); height: width
                            sourceSize: Qt.size(Theme.appIconSizeDisplay * 2, Theme.appIconSizeDisplay * 2)
                            source: card.icon
                            opacity: card.minimized ? 0.45 : 1
                        }
                    }
                    Image {
                        id: picture
                        objectName: "switcherCardPicture"
                        readonly property real aspect: implicitHeight > 0 ? implicitWidth / implicitHeight : 0
                        anchors.centerIn: parent
                        width: aspect > 0 ? Math.min(box.width, box.height * aspect) : 0
                        height: aspect > 0 ? Math.min(box.height, box.width / aspect) : 0
                        source: card.window && card.window.picture ? card.window.picture : ""
                        // A new picture has a new name; the old one is not wanted again, but
                        // stays until the new one has loaded.
                        cache: false
                        retainWhileLoading: true
                        smooth: true; mipmap: true
                        opacity: reveal * (card.minimized ? 0.5 : 1)
                        // Whether it has a picture to show: once one has loaded, until there is
                        // none. The first fades in over the icon standing in for it, unless it
                        // was there as the card was made, as the taskbar's tiles have it; the
                        // next ones take its place at once.
                        property bool shown: false
                        property bool made: false
                        property real reveal: 1
                        onStatusChanged: {
                            if (status === Image.Ready && !shown) {
                                shown = true
                                if (made && Theme.durationNormal > 0)
                                    fadeIn.restart()
                            } else if (status !== Image.Ready && status !== Image.Loading) {
                                shown = false
                                fadeIn.stop()
                                reveal = 1
                            }
                        }
                        NumberAnimation on reveal {
                            id: fadeIn
                            running: false
                            from: 0; to: 1
                            duration: Theme.durationNormal; easing.type: Theme.easing
                        }
                        readonly property bool pictured: shown || status === Image.Ready
                        visible: pictured
                        // Its proportions set its card's width.
                        onAspectChanged: if (pictured) cards.noteAspect(card.windowId, aspect)
                        onPicturedChanged: if (pictured) cards.noteAspect(card.windowId, aspect)
                        Component.onCompleted: {
                            made = true
                            if (pictured)
                                cards.noteAspect(card.windowId, aspect)
                        }
                        layer.enabled: Theme.effects
                        layer.effect: MultiEffect {
                            maskEnabled: true
                            maskSource: pictureMask
                            maskThresholdMin: 0.5
                            maskSpreadAtMin: 1
                        }
                    }
                    Item {
                        id: pictureMask
                        anchors.fill: picture
                        visible: false
                        layer.enabled: Theme.effects
                        Rectangle { anchors.fill: parent; radius: Theme.radiusSmall }
                    }
                    // Badges on the picture's corners, as the icon grid has them on the icon's: a
                    // dot in the urgent colour for a window asking for attention, a dash for a
                    // minimized one.
                    Rectangle {
                        objectName: "switcherUrgent"
                        visible: card.urgent
                        x: parent.width - width + Theme.spacingXS; y: -Theme.spacingXS
                        width: Theme.iconSizeSmall; height: width; radius: width / 2
                        color: Theme.urgent; border.width: 2; border.color: Theme.surface
                    }
                    Rectangle {
                        objectName: "switcherMinimized"
                        visible: card.minimized
                        x: parent.width - width + Theme.spacingXS
                        y: parent.height - height + Theme.spacingXS
                        width: Theme.iconSize + Theme.spacingS; height: width; radius: width / 2
                        color: Theme.surfaceRaised; border.color: Theme.border
                        Icon {
                            anchors.centerIn: parent
                            name: "minus"; size: Theme.iconSizeSmall; color: Theme.textMuted
                        }
                    }
                }
                MouseArea {
                    id: pick
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shell.switcherPick(card.index)
                }
            }
        }
    }
}
