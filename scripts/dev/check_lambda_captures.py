# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Finds lambda captures that the lambda never uses.

    python scripts/dev/check_lambda_captures.py [files...]

Apple Clang (the macOS CI job, warnings as errors) reports an unused capture
(-Wunused-lambda-capture); GCC does not, so the Windows build cannot catch
it. Without arguments it checks every tracked C++ source. A heuristic: it
looks for each captured name in the lambda's body, so a name that only
appears in a comment or a string there is taken as used. Clang itself does
not warn for captures of types that are not trivially copyable (a
shared_ptr, a struct with a QString), which this script still lists.
Exits with 1 when it finds something.
"""

import re
import subprocess
import sys

LAMBDA = re.compile(r'\[([^\[\]]*)\]\s*(\([^()]*(?:\([^()]*\)[^()]*)*\))?\s*(mutable\s*)?(->\s*[\w:<>, ]+\s*)?\{')


def body_end(text, start):
    depth = 0
    i = start
    while i < len(text):
        c = text[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        elif c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                if text[i] == '\\':
                    i += 1
                i += 1
        i += 1
    return len(text)


def check(path):
    found = []
    text = open(path, encoding='utf-8', errors='replace').read()
    # Line comments become spaces (same length, so line numbers stay right).
    text = re.sub(r'//[^\n]*', lambda c: ' ' * len(c.group(0)), text)
    for m in LAMBDA.finditer(text):
        captures = m.group(1).strip()
        if not captures:
            continue
        # A capture list follows = ( , { ; : ? ! | & or return; anything
        # else is an array subscript or an attribute.
        before = text[:m.start()].rstrip()
        if before and before[-1] not in '=(,{;:?!|&' and not before.endswith('return'):
            continue
        brace = m.end() - 1
        body = text[brace:body_end(text, brace)]
        for capture in captures.split(','):
            capture = capture.strip()
            if capture in ('', '=', '&', 'this', '*this'):
                continue
            name = capture.lstrip('&').split('=')[0].strip()
            if not re.search(r'\b' + re.escape(name) + r'\b', body):
                line = text.count('\n', 0, m.start()) + 1
                found.append(f'{path}:{line}: capture {capture!r} is not used')
    return found


def main():
    files = sys.argv[1:]
    if not files:
        files = subprocess.run(['git', 'ls-files', '*.cpp', '*.h', '*.mm'], capture_output=True, text=True,
                               check=True).stdout.split()
    found = [line for path in files for line in check(path)]
    for line in found:
        print(line)
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
