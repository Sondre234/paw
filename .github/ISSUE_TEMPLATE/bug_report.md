---
name: Bug report
about: Something in paw does not work as it should
labels: bug
---

**What happened, and what you expected instead**


**How to make it happen**

1.
2.

**System**

- `paw --version` (both lines):
- GPU and driver (for example "NVIDIA RTX 4090, proprietary 580.xx" or "AMD RX 7800, Mesa 25.2"):
- wlroots package version, if it differs from the one `--version` names:
- Distribution:
- How paw ran: nested inside (which desktop?), `--session` from a text console, or from a
  display manager (which one?):
- Monitors, if the problem involves them (how many, resolutions and scales):

**Configuration**

Your `~/.config/paw/init.lua` (and `theme.lua`, if it has one), or "the default". Leave out
anything private.

```lua

```

**Log**

paw, the shell and the programs they start log to standard error. A session started with
`paw-session`, as the display-manager entry does, keeps it in
`~/.local/state/paw/session.log` (`session.log.old` is the one before). Nested, start it with
`paw 2> ~/paw.log` and make the problem happen. Attach the file, or paste the part
around the problem:

```

```
