# SPDX-License-Identifier: GPL-3.0-or-later
"""The application switcher (switcher_app, Alt + `) lists the focused window's application's
windows alone, on every output and workspace, most recently focused first, and says so with "app";
it steps and confirms as the window switcher does, from the control socket and from the keyboard
(Shift + ` stepping back while Alt is held), never choosing another application's window. An
application with one window opens nothing, and a tab of a group is chosen as any other window."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    features = { groups = true },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    windows = { rules = { { title = "^A3$", output = "HEADLESS-2" } } },
    bindings = {
        { mods = { "Alt" }, key = "grave", action = "switcher_app" },
        { mods = { "Alt", "Shift" }, key = "grave", action = "switcher_app_prev" },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        """By title: workspace, focused, output, visible, group."""
        return {w.title: dict(workspace=w.workspace, focused=w.focused, output=w.output,
                              visible=w.visible, group=w.group) for w in desktop.windows()}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    desktop.detail = lambda: f"windows: {windows()}"
    events = desktop.subscribe()

    def opened(lines):
        """The selection, the window titles and whether it lists one application, of the last
        full switcher announcement."""
        for i in range(len(lines) - 1, -1, -1):
            words = lines[i].split(" ")
            if words[0] == "switcher" and len(lines) - i - 1 >= int(words[3]):
                titles = [line[len("switcher-window "):].split("\t")[1]
                          for line in lines[i + 1:i + 1 + int(words[3])]]
                return int(words[2]), titles, words[4:] == ["app"]
        return None

    def expect(predicate, message):
        """Waits for the switcher lines heard to satisfy `predicate`, then forgets them."""
        lines = []

        def seen():
            lines[:] = events.lines("switcher")
            return predicate(lines)
        wait_for(seen, message, detail=lambda: f"events: {lines}")
        events.forget()
        return lines

    def switched(message):
        """The app switcher's opening: its selection and titles, after checking it lists one
        application's windows, A's."""
        selected, titles, app = opened(expect(opened, message))
        assert app, "the app switcher did not say it lists one application"
        assert all(title.startswith("A") for title in titles), titles
        return selected, titles

    def focus(title):
        """Focuses `title` through the window switcher."""
        msg("switcher")
        _, titles, app = opened(expect(opened, "the window switcher opened"))
        assert not app, "the window switcher said it lists one application"
        msg("switcher_confirm", str(titles.index(title) + 1))
        wait_for(lambda: focused() == title, f"{title} focused")
        expect(lambda l: "switcher-close" in l, "the window switcher closed")

    def opens_nothing(open, message):
        """Running `open` opens no switcher and leaves the focus where it was."""
        before = focused()
        open()
        desktop.stays(lambda: not events.lines("switcher") and focused() == before, message)

    clients = {}

    def open_window(title, app_id):
        clients[title] = desktop.open_window(probe, title, app_id, focused=True)

    # A1 and B on workspace 1 of HEADLESS-1, A2 on its workspace 2.
    open_window("A1", "app.a")
    open_window("B", "app.b")
    msg("output", "HEADLESS-1", "workspace", "2")
    open_window("A2", "app.a")

    # A's windows alone, the one focused before A2 selected; B, focused in between, is not one.
    msg("switcher_app")
    selected, titles = switched("the app switcher opened")
    assert titles == ["A2", "A1"] and selected == 1, (titles, selected)
    msg("switcher_app")
    msg("switcher_app_prev")
    expect(lambda l: l == ["switcher-select 0", "switcher-select 1"], "steps both ways")
    msg("switcher_confirm")
    wait_for(lambda: focused() == "A1" and windows()["A1"]["visible"] and
             not windows()["A2"]["visible"], "A1 focused on workspace 1")
    expect(lambda l: l == ["switcher-close"], "closed on confirming")
    # Backward it opens on the least recent.
    msg("switcher_app_prev")
    selected, titles = switched("the app switcher opened backward")
    assert titles == ["A1", "A2"] and selected == 1, (titles, selected)
    msg("switcher_cancel")
    expect(lambda l: l == ["switcher-close"], "cancelled")
    desktop.stays(lambda: focused() == "A1", "cancelling switched")

    # The window switcher still lists every window, and says nothing of an application.
    msg("switcher")
    _, titles, app = opened(expect(opened, "the window switcher opened"))
    assert titles == ["A1", "A2", "B"] and not app, (titles, app)
    msg("switcher_cancel")
    expect(lambda l: l == ["switcher-close"], "the window switcher cancelled")

    # A3 on the other output; the keyboard: Alt + ` and releasing Alt picks the one before,
    # Alt + Shift + ` the least recent, and Shift + ` with Alt held steps back.
    open_window("A3", "app.a")
    wait_for(lambda: windows()["A3"]["output"] == "HEADLESS-2", "A3 on HEADLESS-2")
    events.forget()
    ALT, SHIFT, GRAVE = 56, 42, 41
    press = desktop.keyboard()
    press(ALT, GRAVE)
    wait_for(lambda: focused() == "A1", "Alt + ` picks the window focused before, A1")
    selected, titles = switched("Alt + ` opened it")
    assert titles == ["A3", "A1", "A2"] and selected == 1, (titles, selected)
    press(ALT, SHIFT, GRAVE)
    wait_for(lambda: focused() == "A2", "Alt + Shift + ` picks the least recent, A2")
    expect(lambda l: "switcher-close" in l, "closed on releasing Alt")
    press.down(ALT)
    press(GRAVE)        # A2, A1, A3: A1 selected
    press(SHIFT, GRAVE)  # back to A2
    press(SHIFT, GRAVE)  # around to A3
    press.up(ALT)
    wait_for(lambda: focused() == "A3", "Shift + ` stepped back around to A3")
    lines = expect(lambda l: "switcher-close" in l, "closed on releasing Alt")
    assert [l for l in lines if l.startswith("switcher-select")] == \
        ["switcher-select 0", "switcher-select 2"], lines

    # B is its application's only window: nothing opens, from the socket or the keyboard.
    focus("B")
    opens_nothing(lambda: msg("switcher_app"), "switcher_app opened for one window")
    opens_nothing(lambda: press(ALT, GRAVE), "Alt + ` opened for one window")

    # A tab of a group: A4 joins A1's group, and the app switcher brings A1's tab forward.
    focus("A1")
    msg("group_toggle")
    wait_for(lambda: windows()["A1"]["group"] != 0, "A1 grouped")
    open_window("A4", "app.a")
    wait_for(lambda: windows()["A4"]["group"] == windows()["A1"]["group"] and
             not windows()["A1"]["visible"], "A4 a tab over A1")
    msg("switcher_app")
    selected, titles = switched("the app switcher opened over a group")
    assert titles[:2] == ["A4", "A1"] and selected == 1 and len(titles) == 4, (titles, selected)
    msg("switcher_confirm")
    wait_for(lambda: focused() == "A1" and windows()["A1"]["visible"] and
             not windows()["A4"]["visible"], "A1's tab brought forward")
print("Application switcher passed")
