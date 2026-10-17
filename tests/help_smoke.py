# SPDX-License-Identifier: GPL-3.0-or-later
"""`paw help` lists every command by group, `paw help COMMAND` says more of one, and `paw help
msg` lists every action and query; `paw --help` is the same list. In a running session `paw msg
get` alone lists the queries, and a query of no such name is refused as one rather than read as
an action."""
from pathlib import Path
import os
import re
import subprocess
import sys

import harness

paw = str(Path(sys.argv[1]).resolve())
features = (Path(__file__).resolve().parent.parent / "docs" / "features.md").read_text()
env = {key: value for key, value in os.environ.items() if key != "PAW_SOCKET"}


def run(*words, ok=True):
    result = subprocess.run([paw, *words], env=env, capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == ok, (words, result.returncode, result.stdout, result.stderr)
    return result.stdout if ok else result.stderr


overview = run("help")
assert run("--help") == overview and run("-h") == overview, "paw --help is not paw help"
commands = re.findall(r"^  paw ([a-z]+) ", overview, re.M)
for name in ("msg", "top", "doctor", "config", "import", "rice", "fetch", "tour", "cat",
             "shake", "gravity", "party", "wrapped", "xray", "heatmap", "showcase", "help"):
    assert name in commands, (name, overview)
for group in ("The running session:", "Configuration and looks:", "Showing off:", "Help:"):
    assert group in overview, (group, overview)
assert all(len(line) <= 80 for line in overview.splitlines()), overview

# Each command says more of itself, and has a section of the documentation to point at.
for name in commands:
    text = run("help", name)
    assert text.startswith(f"paw {name}"), (name, text)
    if name not in ("msg", "import", "help"):
        assert re.search(rf"^### .*\bpaw {name}\b", features, re.M), \
            f"docs/features.md has no section for paw {name}"
assert "pack [PROFILE]" in run("help", "rice")
assert "no command nosuch; there are msg," in run("help", "nosuch", ok=False)

# msg lists every action and query, the queries the same as a running session's.
text = run("help", "msg")
actions = text.split("Actions")[1].split("Queries")[0]
for action in ("workspace", "toggle_overview", "keep_awake_toggle", "pick_color", "shortcuts"):
    assert re.search(rf"\b{action}\b", actions), (action, text)
listed = text.split("Queries")[1].split(":", 1)[1].split()
assert "windows" in listed and "workspaces" in listed, text

with harness.Compositor(paw, "return { version = 1, xwayland = false }\n") as desktop:
    queries = desktop.msg("get").split()
    assert queries == listed, (queries, listed)
    assert "no query nosuch; paw msg get lists them" in desktop.msg("get", "nosuch", ok=False)
    assert "no query" in desktop.msg("get", "x" * 500, ok=False)
    assert desktop.msg("get", "workspace") == "1\n", "a real query still answers"
    assert "paw help msg" in desktop.run().stderr
print("paw help lists every command, and paw msg get every query")
