# SPDX-License-Identifier: GPL-3.0-or-later
"""The documentation matches the code: every binding action the configuration accepts is
described in the README or docs/features.md, and every action the example configuration binds
exists. docs/architecture.md lists every source file of the compositor and every QML file of the
shell."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent
source = (root / "src/config.cpp").read_text()
table = source[source.index("action_table[] = {"):]
table = table[:table.index("};")]
actions = re.findall(r'\{"([a-z_0-9]+)",\s*SH_', table)
assert len(actions) > 80, f"the action table was not found ({len(actions)} entries)"

docs = (root / "README.md").read_text() + (root / "docs/features.md").read_text()
missing = [name for name in actions
           if not re.search(rf"(?<![a-z_0-9]){name}(?![a-z_0-9])", docs)]
assert not missing, f"actions the README and docs/features.md never mention: {missing}"

example = (root / "config/init.lua").read_text()
active = [line for line in example.splitlines() if not line.lstrip().startswith("--")]
bound = set(re.findall(r'action = "([a-z_0-9]+)"', "\n".join(active)))
# Bound or only in a comment, an action named in the example must exist.
commented = set(re.findall(r'action = "([a-z_0-9]+)"', example))
unknown = sorted(commented - set(actions) - {"none"})
assert not unknown, f"config/init.lua mentions actions the code does not have: {unknown}"
architecture = (root / "docs/architecture.md").read_text()
sources = sorted(p.name for p in (root / "src/compositor").glob("*.[ch]"))
unlisted = [name for name in sources if f"`{name}`" not in architecture]
assert not unlisted, f"docs/architecture.md does not list these compositor files: {unlisted}"
qml = sorted(p.name for p in (root / "shell").glob("*.qml"))
unlisted = [name for name in qml if f"`{name}`" not in architecture]
assert not unlisted, f"docs/architecture.md does not list these QML files of the shell: {unlisted}"

print(f"{len(actions)} actions documented; {len(bound)} bound by default")
