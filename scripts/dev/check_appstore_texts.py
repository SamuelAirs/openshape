# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Checks the App Store texts in docs/APP_STORE.md against Apple's limits.

    python scripts/dev/check_appstore_texts.py [docs/APP_STORE.md]

Each text to paste into App Store Connect is a ```text block right after a
marker line such as

    <!-- appstore:description max-chars=4000 -->
    <!-- appstore:keywords max-bytes=100 -->

(characters as App Store Connect counts them, or UTF-8 bytes where Apple
says bytes). Keywords must also be comma-separated without spaces, each
longer than two characters (Apple: "each greater than two characters"),
none twice. Texts must not contain placeholder markers ("TODO", "[OWNER").
Prints one line per text and exits with the number of problems. Standard
library only.
"""

import re
import sys

MARKER = re.compile(r"<!--\s*appstore:([\w-]+)\s+max-(chars|bytes)=(\d+)\s*-->")


def blocks(markdown):
    """(name, unit, limit, text) for every marked block."""
    lines = markdown.split("\n")
    i = 0
    while i < len(lines):
        m = MARKER.search(lines[i])
        i += 1
        if not m:
            continue
        while i < len(lines) and not lines[i].startswith("```"):
            i += 1
        i += 1  # the opening fence
        body = []
        while i < len(lines) and not lines[i].startswith("```"):
            body.append(lines[i])
            i += 1
        yield m.group(1), m.group(2), int(m.group(3)), "\n".join(body).strip("\n")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "docs/APP_STORE.md"
    with open(path, encoding="utf-8", newline="") as f:
        markdown = f.read().replace("\r\n", "\n")
    problems = 0
    seen = 0
    for name, unit, limit, text in blocks(markdown):
        seen += 1
        size = len(text.encode("utf-8")) if unit == "bytes" else len(text)
        ok = 0 < size <= limit
        notes = []
        for marker in ("TODO", "[OWNER", "lorem"):
            if marker.lower() in text.lower():
                ok = False
                notes.append("contains the placeholder %r" % marker)
        if name == "keywords":
            words = text.split(",")
            if any(w != w.strip() or not w for w in words):
                ok = False
                notes.append("spaces or empty entries around commas")
            if len(set(words)) != len(words):
                ok = False
                notes.append("a keyword appears twice")
            short = [w for w in words if len(w.strip()) <= 2]
            if short:
                ok = False
                notes.append("keywords of two characters or fewer: " + ", ".join(short))
        print("[%s] %s: %d of %d %s%s" % ("PASS" if ok else "FAIL", name, size, limit, unit,
                                          (" (" + "; ".join(notes) + ")") if notes else ""))
        problems += 0 if ok else 1
    if seen == 0:
        print("[FAIL] no marked texts found in %s" % path)
        problems += 1
    return problems


if __name__ == "__main__":
    sys.exit(main())
