# Compositor performance

Numbers come from `tools/bench/bench.py`, a headless benchmark: it starts `paw --headless`
with the pixman renderer (no GPU, no real session), maps N clients, drives the compositor
through the control socket, and reports round-trip times and the compositor's own CPU time
from `/proc`. Nothing here touches a real display.

    cmake -S . -B build -G Ninja -DPAW_BUILD_COMPOSITOR=ON && cmake --build build
    python3 tools/bench/bench.py --compositor build/paw \
        --client build/paw-bench-client --windows 40 [--json out.json]

`--quick` runs a few seconds of it and is what the `bench_smoke` test uses. The compositor
counts frames, commits and placements (`paw msg get stats`), which the benchmark reads to
report time per commit and per frame.

Absolute numbers depend on the host and are noisy below a millisecond; compare runs made on
the same machine back to back. What the benchmark cannot measure: real GPU rendering,
scanout, cursor planes, and input-to-photon latency on hardware.

## Results

Use an optimised build (`RelWithDebInfo`, which is what a build with no `CMAKE_BUILD_TYPE` now
gives; before round 2 the default was unoptimised). 40 windows, 1920x1080 headless, window rules and a border on; the machine was
shared with other builds, so times below a millisecond and every p95 move by tens of percent
from run to run. What repeated in two runs of each build:

| measure                              | before      | after       |
|--------------------------------------|------------:|------------:|
| commit handling, workspace switch    | 17-21 us    | 1 us        |
| commit handling, focus_next          | 21-24 us    | 1 us        |
| commit handling, swap_next           | 52-73 us    | 8-28 us     |
| commit handling, toggle_floating x2  | 23-37 us    | 5-29 us     |
| 8 windows redrawing every frame      | 21-22 us per commit, 9.7-10.6% CPU | 4.7-4.9 us, 7.2-7.5% CPU |
| windows placed by toggle_tiling x2   | 450         | 40          |

Layout changes (`layout_next`, `master_grow`, `promote`) did not change measurably: their
time is dominated by the clients' new buffers and wlroots' scene, see below.

Idle is already quiet: about 0.2 wakeups per second and no frames with nothing to do.

## Where the time goes

`bench.py --profile REQUESTS` samples the compositor under gdb (there is no perf on every
host; the tool needs only gdb). With 40 windows and a request stream of `toggle_tiling`:

- about a quarter of the samples are in `refresh_frame`, one third of that in wlroots'
  `wlr_scene_buffer_set_buffer` and its damage/region work, the rest in scene node updates
  from moving and resizing the border and controls;
- `toplevel_configure`, layout computation (`sh_tiling::layout`) and animation setup are
  around 5-15% each;
- freeing the clients' old shm buffers (`close`) and libwayland/libffi dispatch are most of
  the rest. Neither is in paw's code.

The compositor's own code is a small part of the total, so a further large win in these
paths would have to come from doing fewer scene mutations per window (each move or resize of
a scene node makes wlroots revisit the visible region of the whole scene), not from tighter
loops. Scaling from 10 to 40 to 120 windows is about linear per window (`master_grow`: 0.08,
0.08, 0.13 ms per window).

## What changed

- Window opacity rules were matched (regular expressions on app id and title) on every
  window commit. The result is now cached per window and recomputed only when the app id,
  title, focus state or configuration changes (`get stats` shows the count as
  `opacity_rules`; `tests/opacity_rules_smoke.py` checks it stays flat for a window that
  commits every frame, and that title, focus and reload changes still apply).
- The window border and controls were raised to the top of the window's scene tree on every
  commit, which makes the scene recompute the tree. They are now raised only when something
  else was stacked above them.
- Turning tiling on or off placed every window again after each window joined or left, so
  the placements grew with the square of the window count (3750 for 120 windows). The
  windows are now placed once at the end. `bench.py --quick` fails if that regresses.

## Round 2: latency and smoothness under load

The benchmark gained three things. `get stats` reports pointer motion events and their time,
reflows and their time, and `get frame_times` the last 4096 frames' time inside `output_frame` and
the interval between frames. `bench.py --only smooth` is a frame-time scenario: window animations
on, 40 windows of which 8 redraw on every frame, and a request every 250 ms that moves or resizes
every window (layouts, master size, focus, swaps), all of them staying visible. A redrawing window
that is hidden (another workspace, or floating windows piled over it) gets no frame callbacks, so
the frames stop and the idle gap that follows would read as a stall; the scenario avoids that.
`--pointer-probe build/pointer_probe` adds a `pointer` scenario: 2000 virtual pointer moves swept
over the windows, and the same while layout changes keep every window gliding, reported as
compositor time per motion event.

    python3 tools/bench/bench.py --compositor build/paw --client build/paw-bench-client \
        --pointer-probe build/pointer_probe --only smooth pointer --seconds 10

What changed, each measured against the commit before it, `RelWithDebInfo`, 1920x1080 headless
with the pixman renderer. The machine was shared and busy (load average 8 to 19), so single runs
scatter by tens of percent; the tables give the range over repeated back-to-back runs.

| pointer motion, compositor time per event | before | after |
|---|---:|---:|
| 10 windows, idle | 2.8-4.3 us | 2.2-3.1 us |
| 40 windows, idle | 9.3-17.5 us | 5.2-8.2 us |
| 120 windows, idle | 25.8-29.0 us | 12.0-13.4 us |
| 40 windows, while layout changes keep them gliding | 361 us (3 runs, 360-362) | 46-89 us |

- **One hit test per motion event.** A motion asked the scene "what is under the pointer" up to
  four times (window, window controls, tab strip, resize band). The answer is now shared for the
  event, and dropped only when the controls appear or disappear, which changes the scene.
- **Hit tests no longer move every animation.** Input is meant to see where windows will rest, not
  where a glide has drawn them, so each hit test used to put every running animation at its
  resting place and back: two scene updates per animating window, per event. It now does this
  only for animations that could matter at the point (drawn or resting over it, or growing, or a
  closing snapshot). `tests/animation_tests.c` covers which ones are touched.

Measured and left alone:

- **Scene updates per window.** `refresh_frame` already skips unchanged work: wlroots' scene
  setters return early for an unchanged position, size, colour or opacity, and paw raises its
  border and controls only when something was stacked above them. What is left is the scene
  updates for real changes, about 45 us per commit during `layout_next` (6 border updates for a
  size change), 1.8 ms of a 10 to 17 ms request. A microbenchmark on a bare wlroots scene shows
  where that comes from: `wlr_scene_node_set_position` on a window tree costs about 7 us plus
  2.8 us for every leaf node (surface, border rectangle, ...) in the subtree, regardless of the
  window count. Forty gliding windows with a surface and four border rectangles cost about 0.7
  ms of every frame that moves them. Fewer scene nodes per window would cut it, but the only way
  to draw a border with fewer nodes (one rectangle behind the window) shows through translucent
  windows, so it was not done.
- **Coalescing relayouts.** `get stats` counts `reflows` and their time: every action (a layout
  change, tiling toggle, config reload by SIGHUP) runs exactly one, and one that changes little
  (`swap_next`, `toggle_floating`) takes 15 to 35 us for 40 windows (`reflow us` column). Opening
  40 windows one by one runs 41 reflows totalling about 1 ms of a 200 ms burst, and closing them
  40 reflows totalling 8 to 11 ms; wlroots' xdg-shell also already sends a client one configure
  for several size changes within one event loop turn. Deferring reflows to the end of the turn
  would save part of that, and risk stale window positions for code that reads them right after
  (pointer following focus, hit tests), so it was not done.
- **Keybinding lookup.** A linear scan of the bindings takes 13 ns for the 58 default bindings
  and 36 ns for 174, per key press; a hash would not show next to the rest of key handling.
- **Cursor image.** Setting the default cursor on every motion over the bare desktop is already
  cheap: motion over an empty desktop takes 0.3 us in total.
- **Build type.** With the load on this machine a build with no optimisation flags and a
  `RelWithDebInfo` one could not be told apart on the request round trips or pointer motion, since
  wlroots, libwayland and pixman come from the system already optimised and paw's own code
  is a small part. The default is `RelWithDebInfo` because an unoptimised compositor is the wrong
  default for a package, not because a number moved.

Baseline of the frame scenario (no code in round 2 changed it; the runs are the ones to compare
with after a change): frames took p50 1.1 ms, p95 2.2-2.5 ms and p99 3.5-6.6 ms inside
`output_frame`, and arrived every p50 17.1 ms, p95 18.4-20.1 ms, p99 19.9-23.3 ms, with 0 to 5
of 580 frames later than 25 ms and a worst of 23 to 35 ms.

## Not verified

No real GPU, output or input device was involved. The frame times are the compositor's time in
`output_frame` on the headless output with the pixman renderer, so they leave out GPU work,
scanout and vblank, and the pointer times are for virtual pointer events, not a real device.
Direct scanout, the hardware cursor plane
(both left to wlroots' scene and `wlr_cursor`, with dmabuf feedback and explicit sync
enabled), frame pacing against a real vblank and input-to-photon latency need hardware to
measure and have not been.

# Shell performance

`tools/shell_perf.py BUILD_DIR` starts a private headless compositor and the shell against it,
and reports medians over several runs: the time from spawning the shell to the first rendered
frame of each surface, resident (RSS) and proportional (PSS) memory, mapped libraries, threads,
and while idle the CPU use and context switches per second (a context switch is what a wakeup
costs). It reads only `/proc`. `--renderer gpu` (the default) or `software` draws as
`shell.renderer` would, in the environment the compositor gives such a shell, and
`--env NAME=VALUE` changes the environment further. Nothing touches a real session.

    python3 tools/shell_perf.py build --runs 7 --idle 20
    python3 tools/shell_perf.py build --runs 7 --idle 20 --renderer software

## Results

Measured on the development desktop (NVIDIA proprietary driver, Qt 6.11, two surfaces on one
headless 1920x1080 output, the example configuration). "GPU" is what the shell did before
`shell.renderer` existed (Qt's default RHI backend; here it ran on the software GL
implementation because the private compositor has no GPU buffers, so its time and thread count
say little about real GPU drawing). "Before" is `e52cc0e`.

| | GPU, before | software, before | now |
|---|---|---|---|
| first frame, panel | 532 ms | 182 ms | 168 ms |
| RSS | 408 MB | 204 MB | 108 MB |
| PSS | 184 MB | 68 MB | 49 MB |
| mapped libraries | 153 | 123 | 114 |
| threads | 62 | 14 | 12 |
| idle CPU | 0.05 % | 0.05 % | 0.00 % |
| idle context switches per second | 1.3 | 1.4 | 0.0 |

What produced each change:

- **Software renderer by default** (`shell.renderer`, a panel and a wallpaper gain nothing from
  the GPU): the first frame is three times sooner and memory is half, mostly because Qt's
  GL/Vulkan set-up and its render threads never start. `renderer = "gpu"` restores the old
  behaviour. It is read at shell start.
- **The GPU vendor's GLX driver is kept out.** libGLX loads it when the process starts, whether
  or not anything draws with GLX: with NVIDIA that is about 16 MB PSS and 94 MB RSS. The
  compositor starts a software-rendering shell with `__GLX_VENDOR_LIBRARY_NAME` naming a
  vendor that does not exist, and the shell puts the original value back for the applications
  it launches. Startup time did not change.
- **The clock ticks once a minute**, aimed just after the minute changes, instead of every
  second; that was the whole of the idle wakeups (one per second per output).
- **Battery and network state come from kernel messages** (uevents for power supplies and
  interfaces, rtnetlink for link changes) instead of a 5 s poll. A 2 minute timer backs up the
  battery level, and there is none where there is no battery. The change is applied in a
  network namespace in `system_status_test`, so the netlink path is exercised for real.
- **Popups are made on first use** and a moment after startup, not with the panel. Making them
  ahead costs 2.3 MB PSS and keeps the first click free of a load.
- **The taskbar filters** (one per stacked button plus the main one) no longer look at every
  window on every title change, and compute their grouping in one pass instead of one pass per
  window. `tests/task_filter_test.cpp` has 60 windows and 8 filters:

  | | before | after |
  |---|---|---|
  | 100 window title changes | 1.26 to 1.59 s (1.1 million model reads) | 0.6 ms (960 reads) |
  | 20 windows closed and opened, 100 windows | 2.8 to 3.6 s | 22 ms |

  The model also announces only the roles that changed, and nothing when a `done` event
  changed nothing.

Where startup goes now (about 165 ms): roughly 65 ms initialising Qt's Wayland platform,
fonts and the platform theme, about 25 ms creating the desktop surface's first QML tree, 40 ms
for the panel's (the first use of Qt Quick Controls), and 5 ms scanning desktop files. The
dynamic loader takes about 1.5 ms. What is left is Qt's own initialisation, which the shell
cannot shorten without changing what it uses.

## The GPU renderer by default

The shell now draws through the GPU unless `shell.renderer = "software"`. The redesign of the
shell is for machines with a GPU to spare: shadows and other shader effects need Qt Quick's GPU
renderer, which its software renderer cannot stand in for, and the software renderer draws
every frame of an animation on the CPU, which at 144 Hz and above costs more than the GPU would.
The software renderer stays, documented as the choice for a weak machine; the compositor still
keeps the GPU vendor's GLX library out of a shell that uses it.

Measured with `tools/shell_perf.py build --runs 5` and `--renderer software`, one after the
other, on the development desktop (24 threads, NVIDIA proprietary driver, Qt 6.11.2, two
surfaces on one headless output, the example configuration):

| | GPU (llvmpipe here) | software |
|---|---|---|
| first frame, panel | 366 ms (346 to 417) | 149 ms (125 to 193) |
| RSS | 279 MB | 85 MB |
| PSS | 192 MB | 47 MB |
| mapped libraries | 116 | 86 |
| threads | 58 | 9 |
| idle CPU, context switches per second | 0 | 0 |

What this can and cannot say: the private compositor renders with pixman and offers the shell
no GPU buffers, so "GPU" here is Qt's OpenGL renderer on Mesa's software implementation
(llvmpipe), as `QSG_INFO=1` shows. Most of its threads are llvmpipe's rasterizers; its first
frame includes compiling llvmpipe's shaders on the CPU; and its memory is llvmpipe's, not a GPU
driver's. With a real driver the threads, the startup time and the
resident memory are different (NVIDIA's own libraries are about 16 MB PSS and 94 MB RSS before
anything is drawn, as measured before), and buffers move to video memory, which `/proc` does
not show. What carries over is that an idle shell costs nothing either way: no wakeups, no CPU.
The real cost on the GPU has to be measured in a session; it was not, here.

## Notifications and the on-screen display

The shell now also serves `org.freedesktop.Notifications` and draws cards and an on-screen display
(`notifications` and `osd` settings). Two hidden surfaces per output hold them, so their QML is
loaded at start but nothing is drawn or allocated for them until they are shown. Measured on the
same machine as above, medians of two sets of 7 runs each, interleaved to cancel the machine's
load; "before" is `4127e69`, the commit these came after:

| | before | now, no session bus | now, serving on a bus |
|---|---|---|---|
| first frame, panel | 159 ms | 163 to 169 ms | 173 ms |
| RSS | 105.6 MB | 107.1 MB | 107.4 MB |
| PSS | 55.7 MB | 57.1 MB | 57.3 MB |
| mapped libraries | 114 | 115 | 115 |
| threads | 12 | 12 | 12 (12 to 13) |
| idle CPU, context switches | 0 | 0 | 0 |

That is 1.5 to 2 MB and 4 to 14 ms for the extra surfaces, Qt's D-Bus module and the code, and no
wakeups while idle: the daemon has no timer of its own, cards' timers exist only while a card
does, and the backlight is watched through the kernel's uevent socket (with a 1 s poll only where
that socket cannot be opened and a backlight exists). Showing four cards adds about 2 MB RSS to
a 1920x1080 shell and the display 0.4 MB; after 300 notifications in a row the shell's memory
had not grown. `tools/shell_perf.py --private-bus` measures with a session bus of its own; the
tool never lets the shell see the real one.

## After the redesign

The shell after its redesign (the start menu, the clock flyout, Quick Settings, the restyled
menus and overlays, and the motion of the bar and the overlays), against `main` just before the
last part of it (`173d8a6`, which had everything but the motion and the consistency pass), both
built RelWithDebInfo and measured with `tools/shell_perf.py BUILD --runs 5` and
`--renderer software`, one build after the other and then again, on the development desktop
with its own session running (24 threads, NVIDIA proprietary driver, Qt 6.11.2, two surfaces on
one headless output, the example configuration). Medians, the range of the two sets where they
differ:

| | GPU (llvmpipe), before | GPU (llvmpipe), now | software, before | software, now |
|---|---|---|---|---|
| first frame, panel | 411 ms | 423 to 444 ms | 200 ms | 216 to 223 ms |
| RSS | 320 MB | 325 MB | 98 MB | 92 MB |
| PSS | 272 MB | 275 MB | 71 MB | 67 to 68 MB |
| mapped libraries | 120 | 120 | 90 | 90 |
| threads | 59 | 59 | 7 | 7 |
| idle CPU, context switches per second | 0 | 0 | 0 | 0 |

As before, "GPU" here is Qt's OpenGL renderer on Mesa's llvmpipe, drawn on the CPU, because the
private compositor offers no GPU buffers: its memory and threads are llvmpipe's, and its first
frame includes compiling llvmpipe's shaders. A real GPU's cost was not measured. The first frame
is 10 to 30 ms later with the motion and the shared components, the QML of a few more files to
create; memory moved by a few megabytes either way, within what the session around the
measurement moves it by (the figures in the earlier tables were taken on a quieter machine, and
PSS falls as other processes share the same libraries). Idle stays at nothing: every animation
is a transition or a `Behavior` that runs only as something changes, and the one that runs by
itself, the urgent pulse, stops after a few beats. In one run of five through the GPU, both
before and now, the clock's once-a-minute tick fell inside the ten seconds measured, which shows
as one frame's worth of wakeups (about 9 a second over the ten seconds, llvmpipe's threads
drawing it); the software renderer draws that frame with less than one wakeup a second.

## Not verified

- The shell's motion on a real GPU at 144 or 200 Hz, and what it costs there per frame: the
  measurements above are llvmpipe's, and only say that nothing animates while idle.
- Real GPU drawing, now the default. `renderer = "gpu"` was measured only on the software GL
  implementation, and software rendering was not compared with a GPU on a 4K output, where the
  desktop surface's buffers (three of 33 MB each) are the largest memory cost either way.
- The kernel uevent path (`power_supply`) needs a battery, and this desktop has none. The
  filter for those messages is unit-tested, and the rtnetlink path runs end to end.
- The clock is a coarse timer: Qt may fire it up to 5 % of a minute late, so the displayed
  minute can change up to three seconds after it should.
- The compositor-side start of the shell was checked in a private nested session (memory 65 to
  48 MB PSS, no vendor library mapped), not in a standalone one.
