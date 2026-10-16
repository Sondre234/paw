# SPDX-License-Identifier: GPL-3.0-or-later
"""The exploded view (paw xray): `paw xray`, the xray action and its binding open it on the
focused output, a sheet for the background, one for each window in its layer and one per other
layer, which `get xray` lists bottom to top and draws fanned out up and to the right inside the
output; the windows' places, the focus and the stacking stay as they were, and the screen is the
same to the pixel once it closes. The keys go up and down the stack, the pointer hovers and turns
it, the wheel spreads it; Escape on a headless keyboard, the action, a click beside the stack and
a click on a window's sheet close it, the last going to that window. Windows opening and closing
while it is open get and lose their sheets, many share their layer's, it closes as the session
locks and does not open while it is locked, and it closes with its output. `paw xray --list`
prints each output's stack top first."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, pointer_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = {
        { mods = { "Super" }, key = "x", action = "xray" },
    },
}"""
W, H = 1280, 720
KEY_ESC, KEY_TAB, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END = 1, 15, 103, 108, 102, 107
KEY_0, KEY_EQUAL, KEY_X, KEY_LEFTMETA = 11, 13, 45, 125

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def xray():
        """The state line's words (state, progress, output, hovered) and the entries, each a dict
        with the sheet showing it and where that is drawn."""
        rows = desktop.rows("xray")
        state = rows[0][0].split()
        entries = []
        for r in rows[1:]:
            entries.append(dict(output=r[0], layer=r[1], colour=r[2], kind=r[3], name=r[4],
                                title=r[5], box=tuple(int(n) for n in r[6:10]),
                                sheet=int(r[10]), drawn=tuple(int(n) for n in r[11:15])))
        return dict(state=state[0], progress=int(state[1]), output=state[2],
                    hovered=int(state[3]), entries=entries)

    def sheets(view):
        """The drawn box of each sheet, bottom first, and what each shows."""
        found = {}
        for e in view["entries"]:
            if e["sheet"] >= 0:
                found.setdefault(e["sheet"], (e["drawn"], []))[1].append(e)
        return [found[i] for i in sorted(found)]

    def windows():
        return {w.title: (w.x, w.y, w.width, w.height, w.focused) for w in desktop.windows()}

    def stacking():
        return msg("get", "stacking")

    desktop.detail = lambda: f"xray: {msg('get', 'xray')}"
    for title in ("A", "B", "C"):
        desktop.open_window(probe, title, "zz", focused=True)
    msg("toggle_above")  # C kept above the others
    pointer = desktop.virtual_pointer(pointer_probe, W, H)
    pointer("move", str(W // 2), str(H // 2))
    keys = desktop.keyboard()
    before = (windows(), stacking(), msg("get", "drawn"))
    shot = harness.grab(grim, desktop.env)

    # Closed, it lists the stack all the same, and draws nothing.
    view = xray()
    assert view["state"] == "closed" and view["output"] == "-" and view["hovered"] == -1, view
    assert [(e["layer"], e["kind"], e["title"]) for e in view["entries"]] == [
        ("background", "colour", view["entries"][0]["title"]), ("windows", "window", "A"),
        ("windows", "window", "B"), ("above", "window", "C")], view
    assert all(e["sheet"] == -1 for e in view["entries"])

    # `paw xray` opens it: a sheet for the background and one for each window.
    subprocess.run([compositor, "xray"], env=desktop.env, check=True, timeout=20)
    view = xray()
    assert view["state"] == "open" and view["progress"] == 1000, view
    assert view["output"] == "HEADLESS-1" and view["hovered"] == -1, view
    stack = sheets(view)
    assert [[e["title"] if e["kind"] == "window" else e["kind"] for e in shows]
            for _, shows in stack] == [
        ["colour"], ["A"], ["B"], ["C"]], stack
    for i, (box, _) in enumerate(stack):
        x, y, w, h = box
        assert 0 <= x and 0 <= y and x + w <= W and y + h <= H, f"sheet {i} on the output: {box}"
        assert w < W * 0.75 and abs(w / h - W / H) < 0.4, f"sheet {i} scaled down: {box}"
        if i:
            below = stack[i - 1][0]
            assert x > below[0] and y < below[1], f"sheet {i} up and right of {i - 1}"
    # The windows stay where they are, as they were stacked, and are drawn there.
    assert (windows(), stacking(), msg("get", "drawn")) == before

    # The keys go up and down the stack; Escape closes it.
    for code, hovered in ((KEY_TAB, 0), (KEY_UP, 1), (KEY_END, 3), (KEY_DOWN, 2), (KEY_HOME, 0),
                          (KEY_DOWN, 0)):
        keys(code)
        wait_for(lambda: xray()["hovered"] == hovered, f"key {code} selects sheet {hovered}")
    assert (windows(), stacking()) == before[:2], "keys do nothing else"
    keys(KEY_ESC)
    wait_for(lambda: xray()["state"] == "closed", "Escape closed it")
    view = xray()
    assert all(e["sheet"] == -1 for e in view["entries"]) and view["hovered"] == -1, view
    assert (windows(), stacking(), msg("get", "drawn")) == before
    if shot:
        wait_for(lambda: harness.grab(grim, desktop.env).pixels == shot.pixels,
                 "the screen as it was before")

    # The binding opens and closes it; so does the action.
    keys(KEY_LEFTMETA, KEY_X)
    wait_for(lambda: xray()["state"] == "open", "the binding opened it")
    keys(KEY_LEFTMETA, KEY_X)
    wait_for(lambda: xray()["state"] == "closed", "the binding closed it")
    msg("xray")
    assert xray()["state"] == "open"

    # The pointer hovers the topmost sheet under it and turns the stack; the backdrop is no
    # sheet's.
    at_rest = sheets(xray())
    pointer("move", "8", "8")
    def leaning():
        stack = sheets(xray())
        return stack[3][0][0] - stack[0][0][0] > at_rest[3][0][0] - at_rest[0][0][0]
    wait_for(leaning, "moved left, the stack leans further right")
    assert xray()["hovered"] == -1, "nothing hovered on the backdrop"
    turned = sheets(xray())
    for x, y in ((W // 2, H // 2), (W // 3, 2 * H // 3), (2 * W // 3, H // 3)):
        pointer("move", str(x), str(y))
        def under():
            view = xray()
            stack = sheets(view)
            hovered = view["hovered"]
            if hovered < 0:  # then nothing is lifted, and no sheet has the pointer
                return not any(bx <= x < bx + bw and by <= y < by + bh
                               for (bx, by, bw, bh), _ in stack)
            # Nothing above it contains the pointer, and it does, but for its lift.
            above = any(bx <= x < bx + bw and by <= y < by + bh
                        for (bx, by, bw, bh), _ in stack[hovered + 1:])
            bx, by, bw, bh = stack[hovered][0]
            return not above and bx - 48 <= x < bx + bw + 48 and by - 48 <= y < by + bh + 48
        wait_for(under, f"the sheet under {x}, {y} hovered")

    # The wheel spreads it apart, and 0 sets it straight again.
    keys(KEY_0)
    wait_for(lambda: sheets(xray())[3][0][:2] != turned[3][0][:2], "straight again")
    straight = sheets(xray())
    pointer("scroll", "-30")
    def apart():
        spread = sheets(xray())
        gap = (spread[1][0][0] - spread[0][0][0]) / spread[0][0][2]
        return gap > (straight[1][0][0] - straight[0][0][0]) / straight[0][0][2] * 1.2
    wait_for(apart, "the wheel spread the sheets apart")
    keys(KEY_EQUAL)
    keys(KEY_0)
    wait_for(lambda: [b for b, _ in sheets(xray())] == [b for b, _ in straight],
             "the spread back as it opened")

    # A click beside the stack closes it, and goes to no window.
    pointer("move", "8", "8")
    pointer("click", "left")
    wait_for(lambda: xray()["state"] == "closed", "a click on the backdrop closed it")
    assert windows() == before[0], "no window got the click"

    # A click on a window's sheet goes to that window. The pointer moved there turns the stack;
    # 0 sets it straight again with the pointer there.
    msg("xray")
    view = xray()
    stack = sheets(view)
    target, spread = None, 0
    for spread in range(6):
        stack = sheets(xray())
        (sx, sy, sw, sh), _ = stack[1]
        ax, ay, aw, ah = next(e["box"] for e in xray()["entries"] if e["title"] == "A")
        candidates = [(int(sx + (ax + aw * fx) * sw / W), int(sy + (ay + ah * fy) * sh / H))
                      for fx in (0.1, 0.3, 0.5, 0.7, 0.9) for fy in (0.1, 0.3, 0.5, 0.7, 0.9)]
        free = [(x, y) for x, y in candidates
                if not any(bx <= x < bx + bw and by <= y < by + bh
                           for (bx, by, bw, bh), _ in stack[2:])]
        if free:
            target = free[0]
            break
        keys(KEY_EQUAL)  # spread further, until A's sheet shows past the ones over it
        wait_for(lambda: sheets(xray())[1][0] != stack[1][0], "spread further")
    assert target, f"A's sheet shows nowhere: {stack}"
    laid = sheets(xray())
    pointer("move", str(target[0]), str(target[1]))
    keys(KEY_0)
    for _ in range(spread):
        keys(KEY_EQUAL)
    wait_for(lambda: [b for b, _ in sheets(xray())][2:] == [b for b, _ in laid][2:] and
             xray()["hovered"] == 1, "A's sheet under the pointer, the stack straight")
    pointer("click", "left")
    wait_for(lambda: xray()["state"] == "closed" and windows()["A"][4], "the click went to A")
    after = windows()
    assert {t: w[:4] for t, w in after.items()} == {t: w[:4] for t, w in before[0].items()}

    # Windows opening and closing while it is open get and lose their sheets; past six, the
    # windows share their layer's.
    msg("xray")
    extra = desktop.open_window(probe, "D", "zz")
    def d_sheet():
        view = xray()
        # Until the view takes D in, what it showed keeps the sheets it was on.
        assert all(e["sheet"] >= 0 for e in view["entries"] if e["title"] != "D"), view
        return any(e["title"] == "D" and e["sheet"] >= 0 for e in view["entries"])
    wait_for(d_sheet, "D got a sheet")
    view = xray()
    assert len(sheets(view)) == 5, view
    extra.terminate()
    desktop.reap(extra)
    wait_for(lambda: not any(e["title"] == "D" for e in xray()["entries"]), "D's sheet gone")
    assert len(sheets(xray())) == 4
    many = [desktop.open_window(probe, f"M{i}", "zz") for i in range(4)]
    def shared():
        stack = sheets(xray())
        layers = [shows[0]["layer"] for _, shows in stack]
        return layers == ["background", "windows", "above"] and len(stack[1][1]) == 6
    wait_for(shared, "seven windows share their layers' sheets")
    for process in many:
        process.terminate()
        desktop.reap(process)
    wait_for(lambda: len(sheets(xray())) == 4, "a sheet each again")

    # `paw xray --list` prints the stack top first.
    listed = subprocess.run([compositor, "xray", "--list"], env=desktop.env, check=True,
                            capture_output=True, text=True, timeout=20).stdout
    lines = listed.splitlines()
    assert lines[0] == "HEADLESS-1, top first:", listed
    order = [line.split()[0] for line in lines[1:5]]
    assert order == ["above", "windows", "windows", "background"], listed
    assert "zz — C" in lines[1] and "320×240 at" in lines[1], listed
    assert lines[-1] == "The x-ray view is open on HEADLESS-1.", listed

    # The lock closes it at once, and it does not open while locked.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: xray()["state"] == "closed", "the lock closed it")
    assert "locked" in msg("xray", ok=False)
    keys(KEY_LEFTMETA, KEY_X)
    desktop.stays(lambda: xray()["state"] == "closed", "it does not open while locked")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "unlocked")
    msg("xray")
    assert xray()["state"] == "open"
    msg("xray")

    # Open on an output that goes, it closes with it.
    msg("headless_output", "add", "HEADLESS-2", "800x600")
    wait_for(lambda: any(e["output"] == "HEADLESS-2" for e in xray()["entries"]),
             "the second output listed")
    msg("output", "HEADLESS-2", "xray")
    assert xray()["state"] == "open" and xray()["output"] == "HEADLESS-2"
    msg("headless_output", "remove", "HEADLESS-2")
    wait_for(lambda: xray()["state"] == "closed", "closed with its output")
    msg("xray")
    assert xray()["output"] == "HEADLESS-1"

    # Open and still, it draws nothing: the copies are made again only as what they copy changes.
    def frames():
        return int(desktop.rows("output_frames")[0][1])

    def still():
        """Whether no frame comes for a moment: what opening or closing drew is done."""
        drawn = frames()
        try:
            harness.stays(lambda: frames() == drawn, desktop.processes, "", duration=.15)
            return True
        except AssertionError:
            return False
    wait_for(still, "the opening drawn")
    drawn = frames()
    desktop.stays(lambda: frames() == drawn, "no frame drawn while nothing changes", duration=.5)
    msg("xray")
    assert {t: w[:4] for t, w in windows().items()} == {t: w[:4] for t, w in before[0].items()}
    wait_for(still, "the closing drawn")
    drawn = frames()
    desktop.stays(lambda: frames() == drawn, "no frame drawn once closed", duration=.5)

    # With animations the sheets come apart from the screen and go back into it.
    desktop.reload(CONFIG.replace("enabled = false", "enabled = true, minimize = { duration = 400 }"))
    settled = (windows(), stacking())
    msg("xray")
    view = xray()
    assert view["state"] == "open" and view["progress"] < 1000, view
    wait_for(lambda: xray()["progress"] == 1000, "fanned out")
    msg("xray")
    assert xray()["state"] == "closing", xray()
    wait_for(lambda: xray()["state"] == "closed", "closed back into the screen")
    assert (windows(), stacking()) == settled
print("X-ray passed")
