# SPDX-License-Identifier: GPL-3.0-or-later
"""The examples in the documentation work as written: each binding config/init.lua offers in a
comment, added to the default bindings with its example binding mode uncommented, is accepted (no
duplicate key, no unknown action or mode), and so is every Lua block of the README and
docs/features.md, on top of the default configuration."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

paw = str(Path(sys.argv[1]).resolve())
example = Path(sys.argv[2]).resolve()
lines = example.read_text().splitlines()

# The example binding mode, commented out as a whole (`-- modes = {` to its `-- },`): its lines
# are no bindings of their own, and every snippet is checked with it in place.
first = next(n for n, line in enumerate(lines) if re.match(r"\s*--\s*modes = \{", line))


def inside(line):
    """The text of a commented line, without the comment marker and the space after it."""
    return re.sub(r"^\s*-- ?", "", line)


depth = len(inside(lines[first])) - len(inside(lines[first]).lstrip())
last = next(n for n in range(first + 1, len(lines))
            if inside(lines[n]).startswith(" " * depth + "}"))
modes = ["    " + inside(line) for line in lines[first:last + 1]]

snippets = []
i = 0
while i < len(lines):
    if first <= i <= last:
        i += 1
        continue
    match = re.match(r"\s*--\s*(\{ (?:mods|button|key)\b.*)$", lines[i])
    if not match:
        i += 1
        continue
    text = match[1]
    while not text.rstrip().endswith("},") and i + 1 < len(lines):
        i += 1
        text += " " + re.sub(r"^\s*--\s*", "", lines[i])
    if "action =" in text:
        snippets.append(text)
    i += 1
assert len(snippets) >= 8, f"found only {len(snippets)} commented bindings"

start = next(n for n, line in enumerate(lines) if re.match(r"\s*bindings = \{", line))
failures = []
with tempfile.TemporaryDirectory(prefix="paw-snippets-") as directory:
    for snippet in snippets:
        path = Path(directory) / "init.lua"
        path.write_text("\n".join(lines[:start] + modes + lines[start:start + 1] +
                                  ["        " + snippet] + lines[start + 1:]))
        result = subprocess.run([paw, "--check-config", "--config", str(path)],
                                capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            failures.append((snippet, (result.stdout + result.stderr).strip()))
    # The documentation's Lua blocks: whole configurations, or the entries of one (bindings
    # included).
    root = example.parent.parent
    docs = (root / "README.md").read_text() + (root / "docs/features.md").read_text()
    blocks = re.findall(r"```lua\n(.*?)```", docs, re.S)
    assert len(blocks) >= 5, f"found only {len(blocks)} Lua blocks in the documentation"
    for block in blocks:
        if block.lstrip().startswith("return"):
            source = block
        elif block.lstrip().startswith("{"):
            source = f"return {{ version = 1, extends = 'default', bindings = {{\n{block}\n}} }}"
        else:
            source = f"return {{ version = 1, extends = 'default',\n{block}\n}}"
        path = Path(directory) / "readme.lua"
        path.write_text(source)
        result = subprocess.run([paw, "--check-config", "--config", str(path)],
                                capture_output=True, text=True, timeout=30,
                                env={**os.environ, "PAW_DEFAULT_CONFIG": str(example)})
        if result.returncode != 0:
            failures.append((block.strip().splitlines()[0],
                             (result.stdout + result.stderr).strip()))
for snippet, message in failures:
    print(f"rejected: {snippet}\n  {message}", file=sys.stderr)
assert not failures, f"{len(failures)} documentation examples are rejected"
print(f"{len(snippets)} commented bindings and {len(blocks)} documentation blocks are accepted")
