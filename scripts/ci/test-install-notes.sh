#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Checks the release notes template (packaging/windows/release-notes.md)
# and scripts/ci/install-notes.sh, which fills it in when a tag is
# published (.github/workflows/release.yml). The publish step comes after
# about an hour of building, so release.yml runs this first, on every run,
# and ctest runs it too (tests/CMakeLists.txt):
#   - both variants of the real template come out complete: no block
#     markers or @PLACEHOLDERS@ left; the file names, the tag and the code
#     signing policy link filled in; the SmartScreen note only in the
#     unsigned variant, SignPath's attribution only in the signed one;
#   - a small template gives exactly the expected text for each variant;
#   - broken templates (a block not closed, nested blocks, an <!-- end -->
#     without a block), a wrong signing word and a missing template are
#     refused.
# Prints [PASS]/[FAIL] lines; exits with the number of failures.
#
# Usage (from anywhere; needs bash, awk, grep): scripts/ci/test-install-notes.sh [template]
set -uo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
notes="$root/scripts/ci/install-notes.sh"
template=${1:-$root/packaging/windows/release-notes.md}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
checks=0
failures=0

# result <status> <name>: status 0 passes. (The status comes first so that
# it is taken before any command substitution in the name runs.)
result() {
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then
        echo "[PASS] $2"
    else
        echo "[FAIL] $2"
        failures=$((failures + 1))
    fi
}
# has <file> <text>: the file contains the text, lines joined by single
# spaces as Markdown shows them (the template wraps its sentences).
has() { tr '\n' ' ' < "$1" | tr -s ' ' | grep -qF -- "$2"; }
oneline() { tr '\n' ' ' < "$1"; }

# A release candidate, so that the tag's version (1.2.3-rc1) and the file
# names' version (1.2.3) differ.
version=1.2.3-rc1
app_version=1.2.3
tag=v1.2.3-rc1

# 1. The real template, both variants.
for signing in unsigned signed; do
    out="$work/$signing.md"
    bash "$notes" "$version" "$app_version" "$tag" "$signing" "$template" > "$out" 2> "$work/$signing.err"
    result $? "$signing: install-notes.sh succeeds $(oneline "$work/$signing.err")"
    [ -s "$out" ]
    result $? "$signing: the notes are not empty"
    ! grep -nE '<!-- *(if|end)' "$out"
    result $? "$signing: no block markers left"
    ! grep -nE '@[A-Z_]+@' "$out"
    result $? "$signing: no @PLACEHOLDERS@ left"
    has "$out" "OpenShape-$app_version-windows-x64-setup.exe" && has "$out" "OpenShape-$app_version-windows-x64.zip"
    result $? "$signing: names the installer and the zip of version $app_version"
    has "$out" "OpenShape $version" && has "$out" "\`$tag\`"
    result $? "$signing: names version $version and tag $tag"
    # SignPath Foundation asks for the policy to be linked from the release pages.
    has "$out" "Code signing policy" && has "$out" "https://github.com/SamuelAirs/openshape/blob/$tag/docs/CODE_SIGNING.md"
    result $? "$signing: links the code signing policy of tag $tag"
done
has "$work/unsigned.md" "Run anyway"
result $? "unsigned: has the SmartScreen note (Run anyway)"
! has "$work/unsigned.md" "certificate by SignPath Foundation"
result $? "unsigned: does not claim a SignPath signature"
has "$work/signed.md" "Free code signing provided by SignPath.io, certificate by SignPath Foundation"
result $? "signed: has SignPath's attribution"
! has "$work/signed.md" "Run anyway"
result $? "signed: has no SmartScreen note"

# 2. Exactly the expected text from a small template.
printf '%s\n' 'v=@VERSION@ a=@APP_VERSION@ t=@TAG@ @TAG@' \
    '<!-- if signed -->' 'S' '<!-- end -->' \
    '<!-- if unsigned -->' 'U' '<!-- end -->' \
    'end' > "$work/small.md"
for signing in signed unsigned; do
    if [ "$signing" = signed ]; then block=S; else block=U; fi
    expected="v=$version a=$app_version t=$tag $tag
$block
end"
    actual=$(bash "$notes" "$version" "$app_version" "$tag" "$signing" "$work/small.md" 2>&1)
    [ "$actual" = "$expected" ]
    result $? "small template, $signing: exactly the expected lines (got: ${actual//$'\n'/|})"
done

# 3. Broken templates and arguments are refused: exit status not 0, and a
#    message on stderr.
refused() { # <name> <signing> <template>
    local message status
    message=$(bash "$notes" "$version" "$app_version" "$tag" "$2" "$3" 2>&1 >/dev/null)
    status=$?
    [ "$status" -ne 0 ] && [ -n "$message" ]
    result $? "refused: $1 (exit $status: $message)"
}
broken() { # <file name> <template lines...>
    local file="$work/$1"
    shift
    printf '%s\n' "$@" > "$file"
    echo "$file"
}
refused "a block not closed" signed "$(broken unclosed.md 'a' '<!-- if signed -->' 'b')"
refused "nested blocks" signed \
    "$(broken nested.md '<!-- if signed -->' '<!-- if unsigned -->' 'x' '<!-- end -->' '<!-- end -->')"
refused "an <!-- end --> without a block" unsigned "$(broken stray.md 'a' '<!-- end -->')"
refused "signing 'maybe'" maybe "$template"
refused "a missing template" signed "$work/no-such-template.md"

echo "test-install-notes: $failures failed of $checks ($template)"
exit "$failures"
