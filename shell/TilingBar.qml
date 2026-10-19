// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Paw

// The tiling bar (shell.tiling_bar): the bar a monitor shows in place of the taskbar, or the macOS
// style's dock and menu bar, while it tiles (or always, shell.tiling_bar.always), in Hyprland's style
// as Waybar draws it. Its modules
// stand in three groups, from its left end, in its middle and up to its right end, in the colours,
// type and shape the setting gives: one bar across, an island for each group with the wallpaper
// between them, or a pill for each module. It fills the panel's surface, inset by its margins.
// Its popups are the panel's, opened by the module they belong to as the taskbar's are by its
// buttons, and a right click beside the modules opens the bar's menu.
Item {
    id: tilingBar
    required property var panel
    readonly property var config: shell.tilingBar
    // What the panel reaches, as Taskbar.qml names its own: the bar its popups open by, and the
    // modules that open them, null for one not on the bar.
    readonly property Item barItem: bar
    readonly property Item trayRow: module("tray") ? module("tray").row : null
    readonly property Item clock: module("clock")
    readonly property Item volume: module("volume")
    readonly property Item network: module("network")
    readonly property Item profiles: module("profiles")
    readonly property Item wallpapers: module("wallpapers")
    readonly property Item quickSettings: module("quick_settings")
    // The modules made, by name; `made` counts the changes, for bindings that look one up.
    property var modules: ({})
    property int made: 0
    function module(name) {
        void made
        return modules[name] || null
    }

    // Colours: the bar's (each island's or pill's), its text and icons', the accent's and, on the
    // accent, whichever of the bar's and the text's stands out more; the states laid over a
    // module, as Theme's are, from the text colour.
    readonly property color background: config.color
    readonly property color foreground: config.textColor
    readonly property color accent: config.accent
    readonly property color solid: Qt.rgba(background.r, background.g, background.b, 1)
    readonly property color onAccent: Theme.contrast(accent, solid) >= Theme.contrast(accent, foreground) ? solid : foreground
    readonly property color muted: Theme.alpha(foreground, 0.5)
    readonly property color faint: Theme.alpha(foreground, 0.3)
    readonly property color hoverFill: Theme.alpha(foreground, 0.1)
    readonly property color pressedFill: Theme.alpha(foreground, 0.2)
    readonly property color activeFill: Theme.alpha(foreground, 0.15)
    // Type and sizes: a module is the bar's height but for a small inset (none as a pill of its
    // own), with room either side of what it shows; its icons follow the text's size.
    readonly property string fontFamily: config.font || Theme.fontFamily
    readonly property int fontSize: config.fontSize
    readonly property int iconSize: Math.round(fontSize * 1.35)
    readonly property bool islands: config.style === "islands"
    readonly property bool pills: config.style === "pills"
    readonly property int inset: pills ? 0 : Math.max(2, Math.round(config.height / 8))
    readonly property int moduleHeight: config.height - 2 * inset
    readonly property int padding: Math.round(config.height * 0.3)
    readonly property real moduleRadius: pills ? config.radius : Math.max(0, Math.min(config.radius - inset, moduleHeight / 2))
    // The focused window's title is cut short past a third of the bar.
    readonly property real titleWidth: Math.max(120, bar.width / 3)

    // Steps `steps` workspaces of this output on, stopping at either end, where the windows nudge
    // a little the way the slide would take them, as the taskbar's wheel.
    function stepWorkspace(steps) {
        var state = shell.workspaces[panel.outputName] || ({ current: 1 })
        var target = Math.max(1, Math.min(shell.workspaceCount, state.current + steps))
        if (target !== state.current)
            shell.showWorkspace(panel.outputName, target)
        else if (steps !== 0)
            shell.nudgeWorkspace(panel.outputName, steps > 0 ? 1 : -1)
    }
    // A layout action on this output: layout_next, layout_prev, ...
    function layoutAction(action) {
        panel.closeMenus()
        shell.send("output " + panel.outputName + " " + action)
    }

    // The compositor's notification_history action opens the clock's flyout on the output under
    // the pointer, by the clock, else by the bar's right end.
    Connections {
        target: shell
        function onNotificationsRequested(output) {
            if (output === tilingBar.panel.outputName)
                tilingBar.panel.toggleAudioPopup("clock", tilingBar.clock || rightEnd)
        }
    }

    Rectangle {
        id: bar
        objectName: "tilingBar"
        x: tilingBar.config.marginLeft
        y: tilingBar.config.marginTop
        width: parent.width - tilingBar.config.marginLeft - tilingBar.config.marginRight
        height: tilingBar.config.height
        color: tilingBar.config.style === "bar" ? tilingBar.background : "transparent"
        radius: tilingBar.config.radius
        border.width: tilingBar.config.style === "bar" ? tilingBar.config.borderWidth : 0
        border.color: tilingBar.config.borderColor
        // Frosted glass (shell.blur) under its colour, beneath it.
        Glass {
            objectName: "tilingBarGlass"
            anchors.fill: parent
            z: -1
            visible: Theme.glass && tilingBar.config.style === "bar"
            radius: parent.radius
        }
        // A right click beside the modules opens the bar's menu (tiling off, the start menu, ...).
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: (mouse) => tilingBar.panel.openContextMenu(bar, mouse.x, -1)
        }
        // The wheel anywhere its modules leave it pages through the workspaces, down for the next.
        NotchWheel {
            onNotched: (steps) => tilingBar.stepWorkspace(-steps)
        }
        // What opens by the bar's right end with no module of its own there.
        Item { id: rightEnd; x: parent.width; width: 0; height: parent.height }

        Group { id: leftGroup; objectName: "tilingBarLeft"; host: tilingBar; names: tilingBar.config.left }
        Group {
            id: rightGroup
            objectName: "tilingBarRight"
            host: tilingBar
            names: tilingBar.config.right
            x: bar.width - width
        }
        // In the middle, but clear of the other two where it would reach them.
        Group {
            objectName: "tilingBarCenter"
            host: tilingBar
            names: tilingBar.config.center
            readonly property real gap: 2 * tilingBar.config.spacing
            readonly property real lowest: leftGroup.empty ? 0 : leftGroup.width + gap
            readonly property real highest: (rightGroup.empty ? bar.width : rightGroup.x - gap) - width
            x: Math.max(lowest, Math.min((bar.width - width) / 2, highest))
        }
    }

    // A group of modules: their names, in a row, on an island of its own in that style. One with
    // nothing to show takes no room and draws no island.
    component Group: Rectangle {
        id: group
        // The bar (an inline component sees none of the file's ids).
        required property var host
        required property var names
        readonly property bool empty: row.implicitWidth <= 0
        readonly property real across: host.pills ? 0 : host.inset
        width: empty ? 0 : row.implicitWidth + 2 * across
        height: host.config.height
        color: host.islands && !empty ? host.background : "transparent"
        radius: host.config.radius
        border.width: host.islands && !empty ? host.config.borderWidth : 0
        border.color: host.config.borderColor
        Glass {
            anchors.fill: parent
            z: -1
            visible: Theme.glass && group.host.islands && !group.empty
            radius: parent.radius
        }
        Row {
            id: row
            x: group.across
            anchors.verticalCenter: parent.verticalCenter
            spacing: group.host.pills ? 2 * group.host.config.spacing : group.host.config.spacing
            Repeater {
                model: group.names
                delegate: Loader {
                    required property string modelData
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    sourceComponent: group.host.componentFor(modelData)
                    visible: status === Loader.Ready && item.shown
                    onLoaded: {
                        group.host.modules[modelData] = item
                        group.host.made++
                    }
                    Component.onDestruction: {
                        if (group.host.modules[modelData] === item) {
                            delete group.host.modules[modelData]
                            group.host.made++
                        }
                    }
                }
            }
        }
    }

    function componentFor(name) {
        return ({ launcher: launcherModule, workspaces: workspacesModule, layout: layoutModule,
                  mode: modeModule, window: windowModule, clock: clockModule, tray: trayModule,
                  keyboard: keyboardModule, network: networkModule, volume: volumeModule,
                  battery: batteryModule, notifications: notificationsModule, tiling: tilingModule,
                  profiles: profilesModule, wallpapers: wallpapersModule,
                  quick_settings: quickSettingsModule, power: powerModule })[name] || null
    }

    // A module: an icon (`glyph`) and a label in the bar's colours by default, its state laid over it while
    // it is hovered, pressed or `active` (what it opens is open), on a pill of its own in that
    // style, or on `fill`. `shown` false leaves it off the bar (no battery, nothing in the tray).
    component Module: Button {
        id: module
        // The bar (an inline component sees none of the file's ids).
        required property var host
        property bool shown: true
        property bool active: false
        // Whether it does something when clicked, and so shows its states.
        property bool interactive: true
        property string glyph: ""
        property string label: ""
        property color tint: module.host.foreground
        property color fill: module.host.pills ? module.host.background : "transparent"
        property string tip: ""
        hoverEnabled: true
        implicitHeight: module.host.moduleHeight
        // An icon alone stands in a square.
        readonly property real across: label === "" && glyph !== "" ? (module.host.moduleHeight - module.host.iconSize) / 2
                                                                   : module.host.padding
        leftPadding: across; rightPadding: across
        topPadding: 0; bottomPadding: 0
        Accessible.name: tip || label
        BarTip { panel: module.host.panel; owner: module; text: module.tip }
        background: Rectangle {
            radius: module.host.moduleRadius
            color: module.fill
            border.width: module.host.pills ? module.host.config.borderWidth : 0
            border.color: module.host.config.borderColor
            Glass {
                anchors.fill: parent
                z: -1
                visible: Theme.glass && module.host.pills
                radius: parent.radius
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: !module.interactive ? "transparent"
                     : module.pressed ? module.host.pressedFill : module.active ? module.host.activeFill
                     : module.hovered ? module.host.hoverFill : Theme.alpha(module.host.hoverFill, 0)
                Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
        }
        contentItem: Row {
            spacing: Math.round(module.host.fontSize / 2)
            FadingIcon {
                visible: module.glyph !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: module.glyph
                color: module.tint
                size: module.host.iconSize
            }
            Text {
                visible: module.label !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: module.label
                textFormat: Text.PlainText
                color: module.tint
                font.family: module.host.fontFamily; font.pixelSize: module.host.fontSize
            }
        }
    }

    // The start menu (Launchpad in the macOS style), its button showing shell.start_icon as the
    // taskbar's start button does, else a grid in the accent colour; shell.tiling_bar.launcher_size
    // sizes either, up to the bar's height, a size past the module's reaching into the bar's edges.
    Component {
        id: launcherModule
        Module {
            id: launcher
            host: tilingBar
            objectName: "tilingLauncher"
            readonly property int size: tilingBar.config.launcherSize > 0
                                        ? Math.min(tilingBar.config.launcherSize, tilingBar.config.height)
                                        : tilingBar.iconSize
            active: tilingBar.panel.launcherOpen
            tip: "Applications"
            leftPadding: Math.max(2, (tilingBar.moduleHeight - size) / 2)
            rightPadding: leftPadding
            onClicked: tilingBar.panel.launcherOpen = !tilingBar.panel.launcherOpen
            contentItem: Item {
                implicitWidth: launcher.size; implicitHeight: launcher.size
                scale: launcher.pressed ? Theme.pressScale : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                Icon {
                    anchors.centerIn: parent
                    visible: !picture.visible
                    name: "layout-grid"; color: tilingBar.accent; size: launcher.size
                }
                Image {
                    id: picture
                    objectName: "tilingLauncherPicture"
                    anchors.centerIn: parent
                    width: tilingBar.config.launcherSize > 0 ? launcher.size : Math.round(tilingBar.iconSize * 1.2)
                    height: width
                    sourceSize: Qt.size(width * 2, height * 2)
                    source: shell.startIcon
                    fillMode: Image.PreserveAspectFit
                    smooth: true; mipmap: true
                    visible: status === Image.Ready
                }
            }
        }
    }

    // This output's workspaces (shell.workspaces_shown of them, around the current one): numbers
    // or names with the current one on a pill in the accent colour, dots with the current one
    // drawn long, or pictures (shell.tiling_bar.workspace_icons) on round buttons, the current
    // one's larger on a ring in the accent colour; those with windows brighter, those asking for
    // attention in the urgent colour.
    Component {
        id: workspacesModule
        Module {
            id: workspaces
            host: tilingBar
            objectName: "tilingWorkspaces"
            interactive: false
            readonly property var workspaceState: shell.workspaces[tilingBar.panel.outputName] || ({ current: 1, occupied: [] })
            readonly property int count: shell.workspacesShown > 0 ? Math.min(shell.workspacesShown, shell.workspaceCount)
                                                                  : shell.workspaceCount
            readonly property int first: Math.max(1, Math.min(shell.workspaceCount - count + 1,
                                                               workspaceState.current - Math.floor((count - 1) / 2)))
            readonly property var icons: tilingBar.config.workspaceIcons || []
            readonly property bool pictures: icons.length > 0
            readonly property bool dots: !pictures && tilingBar.config.dots
            shown: shell.workspaceCount > 1
            leftPadding: tilingBar.pills ? tilingBar.padding / 2 : 0
            rightPadding: leftPadding
            contentItem: Row {
                spacing: workspaces.dots ? Math.round(tilingBar.fontSize / 2) : 2
                Repeater {
                    model: workspaces.count
                    delegate: Button {
                        id: workspace
                        required property int index
                        readonly property int number: workspaces.first + index
                        readonly property bool current: workspaces.workspaceState.current === number
                        readonly property bool occupied: workspaces.workspaceState.occupied.indexOf(number) >= 0
                        readonly property bool urgent: (workspaces.workspaceState.urgent || []).indexOf(number) >= 0
                        readonly property string name: shell.workspaceNames[number - 1] || ""
                        // Its picture, the list starting over past its last one.
                        readonly property url picture: workspaces.pictures ? workspaces.icons[(number - 1) % workspaces.icons.length] : ""
                        readonly property bool pictured: workspaces.pictures && workspacePicture.status !== Image.Error
                        readonly property color tint: urgent && !current ? Theme.urgent
                                                    : current ? (workspaces.dots ? tilingBar.accent : tilingBar.onAccent)
                                                    : occupied || hovered ? tilingBar.foreground : tilingBar.muted
                        // A dot's size, and a number's pill's height.
                        readonly property int dot: Math.max(6, Math.round(tilingBar.fontSize * 0.6))
                        readonly property real cell: Math.min(tilingBar.moduleHeight, Math.round(tilingBar.fontSize * 1.9))
                        objectName: "tilingWorkspace" + number
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        hoverEnabled: true
                        padding: 0
                        implicitHeight: workspaces.dots || pictured ? tilingBar.moduleHeight : cell
                        implicitWidth: pictured ? tilingBar.moduleHeight
                                     : workspaces.dots ? (current ? 3 * dot : dot) + Math.round(tilingBar.fontSize / 3)
                                     : Math.max(cell, workspaceText.implicitWidth + cell / 2) + (current ? Math.round(cell / 2) : 0)
                        opacity: !pictured || current || urgent ? 1 : occupied || hovered ? 0.9 : 0.55
                        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        Behavior on implicitWidth { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        onClicked: {
                            tilingBar.panel.closeMenus()
                            if (!current)
                                shell.showWorkspace(tilingBar.panel.outputName, number)
                        }
                        // Resting on it shows a picture of it, as on the taskbar (WorkspaceCard.qml).
                        onHoveredChanged: tilingBar.panel.hoverWorkspace(workspace, hovered)
                        onPressedChanged: if (pressed) tilingBar.panel.closeWorkspaceCard()
                        Accessible.name: "Workspace " + number + (name ? " " + name : "") + (urgent ? " (needs attention)" : "")
                        BarTip {
                            panel: tilingBar.panel; owner: workspace
                            visible: !tilingBar.panel.workspaceCards && owner.hovered && !owner.pressed && !panel.menuOpen
                            text: "Workspace " + workspace.number + (workspace.name ? ": " + workspace.name : "") +
                                  (workspace.occupied ? "" : " (empty)") + (workspace.urgent ? ", needs attention" : "")
                        }
                        background: Rectangle {
                            visible: !workspaces.dots
                            radius: workspace.pictured ? height / 2 : Math.min(tilingBar.moduleRadius, height / 2)
                            // A picture keeps its colours: the current one's button is a ring
                            // round a light wash of the accent.
                            color: workspace.pictured
                                   ? (workspace.urgent && !workspace.current ? Theme.alpha(Theme.urgent, 0.35)
                                      : workspace.current ? Theme.alpha(tilingBar.accent, 0.2)
                                      : workspace.pressed ? tilingBar.pressedFill
                                      : workspace.hovered ? Theme.alpha(tilingBar.accent, 0.14) : Theme.alpha(tilingBar.accent, 0))
                                 : workspace.current ? tilingBar.accent
                                 : workspace.pressed ? tilingBar.pressedFill
                                 : workspace.hovered ? tilingBar.hoverFill : Theme.alpha(tilingBar.hoverFill, 0)
                            border.width: workspace.pictured && (workspace.current || workspace.hovered) ? 1 : 0
                            border.color: Theme.alpha(tilingBar.accent, workspace.current ? 0.75 : 0.5)
                            Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        }
                        contentItem: Item {
                            Image {
                                id: workspacePicture
                                objectName: "tilingWorkspacePicture"
                                visible: workspace.pictured
                                anchors.centerIn: parent
                                // 22 and 28 of Waybar's 30-pixel buttons: the current one in bloom.
                                width: Math.round(tilingBar.moduleHeight * (workspace.current ? 0.93 : 0.73))
                                height: width
                                sourceSize: Qt.size(2 * tilingBar.moduleHeight, 2 * tilingBar.moduleHeight)
                                source: workspace.picture
                                fillMode: Image.PreserveAspectFit
                                smooth: true; mipmap: true
                                Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                            }
                            Text {
                                id: workspaceText
                                visible: !workspaces.dots && !workspace.pictured
                                anchors.centerIn: parent
                                text: workspace.name || workspace.number
                                textFormat: Text.PlainText
                                color: workspace.tint
                                font.family: tilingBar.fontFamily; font.pixelSize: tilingBar.fontSize
                                font.weight: workspace.current ? Font.Bold : Font.Normal
                                Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                            }
                            Rectangle {
                                objectName: "tilingWorkspaceDot"
                                visible: workspaces.dots
                                anchors.centerIn: parent
                                width: workspace.current ? 3 * workspace.dot : workspace.dot
                                height: workspace.dot; radius: height / 2
                                color: workspace.current || workspace.urgent ? workspace.tint
                                     : workspace.occupied || workspace.hovered ? tilingBar.foreground : tilingBar.faint
                                Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                                Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                            }
                        }
                    }
                }
            }
        }
    }

    // The tiling layout of the workspace shown, which a click steps to the next (a right click to
    // the one before, the wheel either way).
    Component {
        id: layoutModule
        Module {
            id: layout
            host: tilingBar
            objectName: "tilingLayout"
            readonly property string name: (shell.workspaces[tilingBar.panel.outputName] || {}).layout || ""
            // Floating windows, under shell.tiling_bar.always, have no layout to show.
            shown: name !== "" && tilingBar.panel.tiling
            glyph: name === "monocle" ? "square" : name === "master" ? "layout-panel-left"
                : name === "scroll" ? "app-window" : "layout-grid"
            label: name
            tip: "Tiling layout: " + name + "\nClick: next layout"
            onClicked: tilingBar.layoutAction("layout_next")
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onPressed: tilingBar.layoutAction("layout_prev")
            }
            NotchWheel {
                onNotched: (steps) => tilingBar.layoutAction(steps > 0 ? "layout_prev" : "layout_next")
            }
        }
    }

    // The binding mode in use while it is not the default one, on the accent; a click leaves it.
    Component {
        id: modeModule
        Module {
            host: tilingBar
            objectName: "tilingMode"
            shown: shell.bindingMode !== ""
            label: shell.bindingMode
            fill: tilingBar.accent
            tint: tilingBar.onAccent
            tip: "Binding mode: " + shell.bindingMode + "\nClick: leave it"
            onClicked: { tilingBar.panel.closeMenus(); shell.send("mode default") }
        }
    }

    // The focused window's title, cut short past a third of the bar; whole in its tip.
    Component {
        id: windowModule
        Module {
            id: window
            host: tilingBar
            objectName: "tilingWindow"
            interactive: false
            readonly property int task: tilingBar.panel.allWindows.activeTask
            readonly property string title: task >= 0 && focused.count > 0 ? focused.windows[0].title : ""
            TaskFilter { id: focused; sourceModel: tilingBar.panel.taskSource; taskId: window.task }
            shown: title !== ""
            // Where the focused window shows, for the compositor to minimize it into: its title,
            // wherever the modules around it move it.
            readonly property rect placedRect: {
                for (var item = window; item && item !== tilingBar.panel; item = item.parent)
                    void [item.x, item.y, item.width, item.height]
                void [tilingBar.panel.surfaceLeft, tilingBar.panel.surfaceTop, shown, visible]
                if (!shown || !visible)
                    return Qt.rect(0, 0, 0, 0)
                var point = mapToItem(tilingBar.panel, 0, 0)
                return Qt.rect(tilingBar.panel.surfaceLeft + point.x, tilingBar.panel.surfaceTop + point.y,
                               width, height)
            }
            function placeWindow() { tilingBar.panel.placeWindows(window, task >= 0 ? [task] : [], placedRect, 0) }
            onPlacedRectChanged: placeWindow()
            onTaskChanged: placeWindow()
            Component.onCompleted: placeWindow()
            tip: titleText.truncated ? title : ""
            contentItem: Text {
                id: titleText
                text: window.title
                textFormat: Text.PlainText
                width: Math.min(implicitWidth, tilingBar.titleWidth)
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: tilingBar.foreground
                font.family: tilingBar.fontFamily; font.pixelSize: tilingBar.fontSize
            }
        }
    }

    // The time, as shell.tiling_bar.clock_format writes it; a click opens the flyout with the
    // calendar and the notifications, a right click toggles do-not-disturb.
    Component {
        id: clockModule
        Module {
            id: clock
            host: tilingBar
            objectName: "tilingClock"
            readonly property var center: shell.notifications
            readonly property int unread: center.serving ? center.unread : 0
            readonly property string format: tilingBar.config.clockFormat
            // Seconds shown wake it every second, else once a minute, just after the minute.
            readonly property bool seconds: format.indexOf("s") >= 0
            property date now: new Date()
            label: Qt.formatDateTime(now, format)
            active: tilingBar.panel.audioPopup === "clock"
            tip: Qt.formatDate(now, "dddd d MMMM yyyy") +
                 (unread > 0 ? "\n" + unread + (unread === 1 ? " unread notification" : " unread notifications") : "") +
                 (center.serving && center.dnd ? "\nDo not disturb" : "")
            onClicked: tilingBar.panel.toggleAudioPopup("clock", clock)
            Timer {
                function untilNext() {
                    var d = new Date()
                    return clock.seconds ? 1010 - d.getMilliseconds() : 60050 - d.getSeconds() * 1000 - d.getMilliseconds()
                }
                interval: untilNext(); running: true; repeat: true
                onTriggered: { clock.now = new Date(); interval = untilNext() }
            }
            onFormatChanged: now = new Date()
            MouseArea {
                anchors.fill: parent
                enabled: clock.center.serving
                acceptedButtons: Qt.RightButton
                onPressed: clock.center.toggleDnd()
            }
        }
    }

    // Applications' status icons, as on the taskbar, while there are any.
    Component {
        id: trayModule
        Module {
            id: tray
            host: tilingBar
            objectName: "tilingTray"
            readonly property Item row: icons
            interactive: false
            shown: shell.widgets.tray && shell.tray.shown > 0
            leftPadding: tilingBar.pills ? tilingBar.padding / 2 : 0
            rightPadding: leftPadding
            contentItem: Row {
                id: icons
                objectName: "tilingTrayIcons"
                Repeater {
                    model: shell.tray
                    delegate: TrayButton {
                        panel: tilingBar.panel
                        width: tilingBar.moduleHeight; height: tilingBar.moduleHeight
                        iconSize: tilingBar.iconSize
                    }
                }
            }
        }
    }

    // The keyboard layout's short name, while the keymap has two or more; a click switches.
    Component {
        id: keyboardModule
        Module {
            host: tilingBar
            objectName: "tilingKeyboard"
            readonly property var layout: shell.keyboardLayout
            shown: (layout.count || 0) > 1
            glyph: "keyboard"
            label: layout.short || ""
            tip: "Keyboard layout: " + (layout.name || layout.short || "") + "\nClick: next layout"
            onClicked: { tilingBar.panel.closeMenus(); shell.send("switch_layout next") }
        }
    }

    // The network, as on the taskbar: Wi-Fi's bars, a plug, or Wi-Fi struck through while the link
    // is down; a click lists the Wi-Fi networks where NetworkManager has a Wi-Fi device.
    Component {
        id: networkModule
        Module {
            id: network
            host: tilingBar
            objectName: "tilingNetwork"
            readonly property var status: tilingBar.panel.statusSource
            readonly property var wifi: tilingBar.panel.wifiSource
            readonly property bool managed: !!wifi && wifi.available
            readonly property string kind: !managed ? (status.networkState === "disconnected" ? "" : status.networkState)
                : wifi.primaryType === "wifi" || (wifi.primaryType === "other" && wifi.ssid !== "") ? "wifi"
                : wifi.primaryType !== "" ? "ethernet" : ""
            readonly property int strength: managed && wifi.ssid !== "" ? wifi.strength : 100
            shown: status.networkState !== "none" || (managed && wifi.hasWifi)
            glyph: kind === "ethernet" ? "ethernet-port" : kind === "" ? "wifi-off" : Theme.wifiIcon(strength)
            tint: kind === "" ? tilingBar.muted : tilingBar.foreground
            active: tilingBar.panel.audioPopup === "wifi"
            tip: !managed ? status.networkText
                : wifi.primaryType === "wifi" ? "Wi-Fi: " + wifi.primaryName + ", signal " + wifi.strength + "%"
                : wifi.primaryType === "ethernet" ? "Wired: " + wifi.primaryName
                : wifi.primaryType !== "" ? "Connected: " + wifi.primaryName
                : !wifi.hasWifi || wifi.enabled ? "Not connected" : "Wi-Fi is off"
            onClicked: if (managed && wifi.hasWifi) tilingBar.panel.toggleAudioPopup("wifi", network)
        }
    }

    // The default output's volume: a click lists the applications', a right click the outputs,
    // a middle click mutes, the wheel turns it up and down.
    Component {
        id: volumeModule
        Module {
            id: volume
            host: tilingBar
            objectName: "tilingVolume"
            readonly property var audio: tilingBar.panel.audioSource
            shown: audio.available
            glyph: audio.muted || audio.volume === 0 ? "volume-x" : audio.volume > 50 ? "volume-2" : "volume-1"
            label: audio.volume + "%"
            tint: audio.muted ? tilingBar.muted : tilingBar.foreground
            active: tilingBar.panel.audioPopup === "mixer" || tilingBar.panel.audioPopup === "outputs"
            tip: "Volume " + audio.volume + "%" + (audio.muted ? ", muted" : "")
            onClicked: tilingBar.panel.toggleAudioPopup("mixer", volume)
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.RightButton)
                        tilingBar.panel.toggleAudioPopup("outputs", volume)
                }
                onClicked: (mouse) => {
                    if (mouse.button === Qt.MiddleButton)
                        volume.audio.toggleMute()
                }
            }
            // Five percent a notch, up for louder.
            NotchWheel {
                invertX: true
                onNotched: (steps) => volume.audio.changeVolume(steps * 5)
            }
        }
    }

    // The battery's charge, only where there is one: an outline filled to it, in the danger
    // colour when nearly empty, the accent's while charging.
    Component {
        id: batteryModule
        Module {
            id: battery
            host: tilingBar
            objectName: "tilingBattery"
            readonly property var status: tilingBar.panel.statusSource
            readonly property bool charging: status.batteryState === "charging"
            interactive: false
            shown: status.batteryPresent
            tint: status.batteryPercent <= 15 && !charging ? Theme.danger : charging ? tilingBar.accent : tilingBar.foreground
            tip: status.batteryText
            contentItem: Row {
                spacing: Math.round(tilingBar.fontSize / 2)
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(tilingBar.iconSize * 1.25); height: Math.round(tilingBar.iconSize * 0.65)
                    Rectangle {
                        width: parent.width - 2; height: parent.height
                        radius: 2; color: "transparent"
                        border.color: battery.tint; border.width: 1
                        Rectangle {
                            x: 2; y: 2; height: parent.height - 4; radius: 1
                            width: Math.max(1, (parent.width - 4) * battery.status.batteryPercent / 100)
                            color: battery.tint
                            Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                        }
                    }
                    Rectangle {
                        x: parent.width - 2; y: parent.height / 3
                        width: 2; height: parent.height / 3
                        color: battery.tint
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: battery.status.batteryPercent + "%"
                    color: battery.tint
                    font.family: tilingBar.fontFamily; font.pixelSize: tilingBar.fontSize
                }
            }
        }
    }

    // The unread notifications and do-not-disturb: a click opens them, a right click toggles it.
    Component {
        id: notificationsModule
        Module {
            id: bell
            host: tilingBar
            objectName: "tilingNotifications"
            readonly property var center: shell.notifications
            shown: center.serving
            glyph: center.dnd ? "bell-off" : "bell"
            label: center.unread > 0 ? String(center.unread) : ""
            tint: center.dnd ? tilingBar.muted : center.unread > 0 ? tilingBar.accent : tilingBar.foreground
            active: tilingBar.panel.audioPopup === "clock"
            tip: (center.dnd ? "Do not disturb, " : "") +
                 (center.unread > 0 ? center.unread + " unread notifications" : "No new notifications") +
                 "\nRight-click: do not disturb"
            onClicked: tilingBar.panel.toggleAudioPopup("clock", bell)
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onPressed: bell.center.toggleDnd()
            }
        }
    }

    // Tiling on this monitor, which a click turns off, the taskbar coming back with the windows
    // floating; with shell.tiling_bar.always the bar stays, the module dimmed, and a click turns
    // tiling on again.
    Component {
        id: tilingModule
        Module {
            host: tilingBar
            objectName: "tilingBarToggle"
            enabled: shell.tilingAvailable
            glyph: "layout-panel-left"
            tint: tilingBar.panel.tiling ? tilingBar.accent : tilingBar.muted
            tip: tilingBar.panel.tiling ? "Tiling on: click for floating windows"
                                        : "Tiling off: click to tile the windows"
            onClicked: { tilingBar.panel.closeMenus(); shell.toggleTiling(tilingBar.panel.outputName) }
        }
    }

    // The appearance profiles, while there are two or more.
    Component {
        id: profilesModule
        Module {
            id: profiles
            host: tilingBar
            objectName: "tilingProfiles"
            shown: shell.profiles.length > 1
            glyph: "palette"
            active: tilingBar.panel.audioPopup === "profiles"
            tip: "Appearance: " + (shell.profile || "default")
            onClicked: tilingBar.panel.toggleAudioPopup("profiles", profiles)
        }
    }

    // The wallpaper picker.
    Component {
        id: wallpapersModule
        Module {
            id: wallpapers
            host: tilingBar
            objectName: "tilingWallpapers"
            glyph: "image"
            active: tilingBar.panel.audioPopup === "wallpapers"
            tip: "Wallpaper"
            onClicked: tilingBar.panel.toggleAudioPopup("wallpapers", wallpapers)
        }
    }

    // Quick Settings, whatever it holds.
    Component {
        id: quickSettingsModule
        Module {
            id: quick
            host: tilingBar
            objectName: "tilingQuickSettings"
            glyph: "toggles"
            active: tilingBar.panel.audioPopup === "quick"
            tip: "Quick settings"
            onClicked: tilingBar.panel.toggleAudioPopup("quick", quick)
        }
    }

    // The power menu by it: lock, suspend and the rest, as far as they may run.
    Component {
        id: powerModule
        Module {
            id: power
            host: tilingBar
            objectName: "tilingPower"
            shown: shell.power.entries.length > 0
            glyph: "power"
            active: tilingBar.panel.audioPopup === "power"
            tip: "Power"
            onClicked: tilingBar.panel.toggleAudioPopup("power", power)
        }
    }
}
