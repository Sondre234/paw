# SPDX-License-Identifier: GPL-3.0-or-later
"""profile_schedule picks the day profile at sunrise and the night one at sunset, on night light's
fixed clock (PAW_NIGHT_LIGHT_TIME): one that passed while paw was not running is applied as it
starts, a profile picked by hand stays until the next, through reloads and restarts, `profile auto`
hands the choice back to the schedule, and a change of the schedule's profiles applies at once."""
from datetime import datetime, timezone
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())


def config(schedule, extra=""):
    return f"""return {{
    xwayland = false,
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "640x480" }} }} }},
    profile = "tsuki",
    profiles = {{
        shiro = {{ appearance = {{ background = "#f2f4f8" }} }},
        tsuki = {{ appearance = {{ background = "#101018" }} }},
        other = {{}},
    }},
    {schedule}
    {extra}
}}"""


SCHEDULE = 'profile_schedule = { day = "shiro", night = "tsuki", sunrise = "07:00", sunset = "20:00" },'


def today_at(hours, minutes=0):
    """Seconds since the epoch at this time of today, in UTC (the tests' TZ)."""
    now = datetime.now(timezone.utc)
    return int(now.replace(hour=hours, minute=minutes, second=0, microsecond=0).timestamp())


with harness.Compositor(compositor, config(SCHEDULE), start=False,
                        env={"TZ": "UTC"}) as desktop:
    state = desktop.root / "state" / "paw"
    msg = desktop.msg

    def in_use():
        return [name for name, used in desktop.rows("profiles") if used == "1"]

    def schedule():
        due, kind, boundary, applied, following = desktop.rows("profile_schedule")[0]
        return due, kind, int(boundary), int(applied), following == "1"

    def start_at(clock):
        desktop.env["PAW_NIGHT_LIGHT_TIME"] = clock
        desktop.start()

    def picks(name):
        before = desktop.reloads()
        msg("profile", name)
        desktop.wait_for(lambda: desktop.reloads() > before and in_use() == [name],
                         f"{name} picked", detail=lambda: in_use())

    # Noon, the configuration starting at night and no sunrise applied yet: this morning's
    # sunrise brings the day profile as paw starts, saved as a pick would be.
    start_at("12:00")
    assert in_use() == ["shiro"], in_use()
    assert (state / "profile").read_text() == "shiro\n"
    sunrise = today_at(7)
    assert (state / "profile-schedule").read_text() == f"{sunrise} sunrise shiro\n"
    assert schedule() == ("shiro", "sunrise", sunrise, sunrise, True), schedule()
    assert "Profile schedule: the sunrise of" in desktop.log.read_text()

    # A profile picked by hand stays: through a reload, and a restart in the same day.
    picks("other")
    assert schedule() == ("shiro", "sunrise", sunrise, sunrise, False), schedule()
    desktop.reload()
    desktop.stays(lambda: in_use() == ["other"], "the pick by hand stays through a reload",
                  duration=.5)
    desktop.stop()
    start_at("13:00")
    assert in_use() == ["other"], in_use()
    assert (state / "profile-schedule").read_text() == f"{sunrise} sunrise shiro\n"

    # `profile auto` hands the choice back at once, as does the profile_auto action.
    msg("profile", "auto")
    desktop.wait_for(lambda: in_use() == ["shiro"], "the schedule's profile back",
                     detail=lambda: in_use())
    assert schedule()[4]
    picks("tsuki")
    msg("profile_auto")
    desktop.wait_for(lambda: in_use() == ["shiro"], "the schedule's profile back by the action",
                     detail=lambda: in_use())
    picks("other")
    desktop.stop()

    # The sunset passed while paw was off: the night profile as it starts, the pick by hand
    # gone with the day.
    start_at("21:00")
    sunset = today_at(20)
    assert in_use() == ["tsuki"], in_use()
    assert (state / "profile-schedule").read_text() == f"{sunset} sunset tsuki\n"
    assert schedule() == ("tsuki", "sunset", sunset, sunset, True), schedule()

    # A new night profile in the schedule applies at once.
    desktop.reload(config(SCHEDULE.replace('night = "tsuki"', 'night = "other"')))
    desktop.wait_for(lambda: in_use() == ["other"], "the schedule's new night profile",
                     detail=lambda: in_use())
    assert (state / "profile-schedule").read_text() == f"{sunset} sunset other\n"

    # Without its own times the schedule follows night light's location, night light off: on
    # the equator at the prime meridian the sun is up at noon UTC, risen near six.
    desktop.reload(config('profile_schedule = { day = "shiro", night = "tsuki" },',
                          "night_light = { latitude = 0, longitude = 0 },"))
    desktop.stop()
    start_at("12:00")
    assert in_use() == ["shiro"], in_use()
    due, kind, boundary, applied, following = schedule()
    assert (due, kind, following) == ("shiro", "sunrise", True), schedule()
    assert today_at(5, 30) < boundary < today_at(6, 30), (boundary, today_at(6))

    # Without a schedule nothing is picked, and `profile auto` says why.
    desktop.reload(config(""))
    assert schedule() == ("-", "-", -1, applied, False), schedule()
    assert "does not set" in msg("profile", "auto", ok=False)
    picks("other")
    desktop.stays(lambda: in_use() == ["other"], "nothing scheduled", duration=.5)
print("profile schedule passed")
