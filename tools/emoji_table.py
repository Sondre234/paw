#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes vendor/emoji/emoji.tsv, the emoji picker's table, from Unicode's emoji-test.txt and
CLDR's English annotations (common/annotations/en.xml); vendor/emoji/README.md says where they
come from.

    tools/emoji_table.py EMOJI_TEST EN_XML OUTPUT

The table lists every fully-qualified emoji in Unicode's order but the components (skin tones and
hair styles alone), a group at a time: a line "@GROUP", then one line per emoji with its text,
its name, its keywords separated by "|" (CLDR's, without the name itself), and, for one that takes
a skin tone, its five forms with one tone throughout, light to dark, separated by spaces; fields
are separated by tabs. Lines starting with "#" are comments."""
import re
import sys
import xml.etree.ElementTree as ElementTree

TONES = [0x1F3FB, 0x1F3FC, 0x1F3FD, 0x1F3FE, 0x1F3FF]
VARIATION = 0xFE0F


def base(codes):
    """What an emoji is without its skin tones and its emoji presentation selectors."""
    return tuple(code for code in codes if code != VARIATION and code not in TONES)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    test, annotations, output = sys.argv[1:4]
    version, group, groups, entries, toned = "?", None, [], [], {}
    with open(test, encoding="utf-8") as lines:
        for line in lines:
            if line.startswith("# Version:"):
                version = line.split(":", 1)[1].strip()
            if match := re.match(r"# group: (.+)", line):
                group = match[1].strip()
            if not line.strip() or line.startswith("#") or group == "Component":
                continue
            fields, _, comment = line.partition("#")
            sequence, status = (field.strip() for field in fields.split(";"))
            if status != "fully-qualified":
                continue
            codes = [int(code, 16) for code in sequence.split()]
            name = re.match(r"\s*\S+\s+E\d+\.\d+\s+(.+)", comment)[1].strip()
            tones = {code for code in codes if code in TONES}
            if tones:
                # Only a tone throughout: mixed ones are not offered.
                if len(tones) == 1:
                    toned.setdefault(base(codes), {})[TONES.index(tones.pop())] = codes
                continue
            if group not in groups:
                groups.append(group)
            entries.append((codes, name, groups.index(group)))
    keywords = {}
    for node in ElementTree.parse(annotations).getroot().iter("annotation"):
        if node.get("type") != "tts" and node.text:
            keywords[node.get("cp").replace(chr(VARIATION), "")] = [
                word.strip() for word in node.text.split("|") if word.strip()]
    text = lambda codes: "".join(map(chr, codes))
    with open(output, "w", encoding="utf-8") as out:
        out.write(f"# The emoji picker's table, made by tools/emoji_table.py from Unicode's\n"
                  f"# emoji-test.txt (Emoji {version}) and CLDR's English annotations: see README.md.\n"
                  f"# © Unicode, Inc., under the Unicode License v3 in LICENSE.\n")
        current = None
        for codes, name, index in entries:
            if index != current:
                out.write(f"@{groups[index]}\n")
                current = index
            words = [word for word in keywords.get(text(codes).replace(chr(VARIATION), ""), [])
                     if word.lower() != name.lower()]
            forms = toned.get(base(codes), {})
            fields = [text(codes), name, "|".join(words)]
            if len(forms) == len(TONES):
                fields.append(" ".join(text(forms[tone]) for tone in range(len(TONES))))
            out.write("\t".join(fields).rstrip("\t") + "\n")
    print(f"{len(entries)} emoji in {len(groups)} groups, "
          f"{sum(len(forms) == len(TONES) for forms in toned.values())} with skin tones")


if __name__ == "__main__":
    main()
