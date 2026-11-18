#!/usr/bin/env python3
"""Keep the Guard switch captions on screen.

gen_ui.py writes a Live label and a Held label over the two segments, then
hides them when their handle does not resolve. On the MPC both segments then
show the parameter name, Guard. This turns those two captions back on.

It edits the two caption blocks in place. A full JSON rewrite would drop the
repeated version keys the overlay widgets need.

Run it after tools/build.py, on the Plugin Skins/TUI.json it just wrote.
"""

import sys


def patch(text, name, label):
    marker = '"name": "%s"' % name
    start = text.find(marker)
    if start < 0:
        sys.exit("TUI.json has no %s caption" % name)
    line_start = text.rfind("\n", 0, start) + 1
    indent = " " * (start - line_start)
    end = text.find("\n" + indent + '"name":', start + len(marker))
    if end < 0:
        end = len(text)
    block = text[start:end]
    if '"text": "%s"' % label not in block:
        sys.exit("%s does not read %s" % (name, label))
    updated = block.replace('"handleName": "Data"', '"handleName": "Invalid"', 1)
    updated = updated.replace(
        '"showWhenDataModelInvalid": "Hide"',
        '"showWhenDataModelInvalid": "Show"',
        1)
    if updated == block:
        sys.exit("%s captions were not updated" % name)
    return text[:start] + updated + text[end:], "%s=%s" % (name, label)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: show_guard_labels.py <Plugin Skins/TUI.json>")
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read()
    shown = []
    for name, label in (("Guard seg 0", "Live"), ("Guard seg 1", "Held")):
        text, note = patch(text, name, label)
        shown.append(note)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("guard captions:", ", ".join(shown))


if __name__ == "__main__":
    main()
