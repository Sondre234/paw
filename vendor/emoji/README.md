# Emoji data

`emoji.tsv` is the shell's emoji picker's table (`shell/emoji.cpp` reads it, compiled into
`paw-shell`), made by `tools/emoji_table.py` from two files of Unicode's:

- `emoji-test.txt` for Emoji 16.0, https://www.unicode.org/Public/emoji/16.0/emoji-test.txt:
  every fully-qualified emoji in Unicode's order, its name and group, and its skin-tone forms;
- CLDR 46's English annotations, `common/annotations/en.xml` in
  https://unicode.org/Public/cldr/46/cldr-common-46.0.zip: the keywords a search finds an emoji by.

Both were downloaded on 2026-10-08. They are © Unicode, Inc., under the Unicode License v3, whose
notice is in `LICENSE` here. Emoji 16.0 is what the colour emoji fonts of 2025 draw (Noto Color
Emoji among them); a newer version's additions show as boxes until the fonts have them.

The table lists the emoji a group at a time, a line `@GROUP` before each group's: per emoji its
text, its name, its keywords separated by `|` and, for one that takes a skin tone, its five forms
with one tone throughout, light to dark, separated by spaces, the fields separated by tabs. The
components (skin tones and hair styles alone) are left out, and so are the forms that mix tones.

To make it again, for another version (Unicode's emoji and the CLDR release that goes with it):

```sh
curl -O https://www.unicode.org/Public/emoji/16.0/emoji-test.txt
curl -O https://unicode.org/Public/cldr/46/cldr-common-46.0.zip
unzip cldr-common-46.0.zip common/annotations/en.xml
python3 tools/emoji_table.py emoji-test.txt common/annotations/en.xml vendor/emoji/emoji.tsv
```
