#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Checks fetch-source.sh, mirror-sources.sh and released-pins.sh without downloading
# (ctest: ios_sources_scripts): made-up archives, pinned in a made-up
# sources.txt, are taken from OPENSHAPE_SOURCES_DIR:
#   - an archive with the pinned SHA-256 is copied as <name>-<version>.<ext>,
#     and one already there is kept;
#   - a wrong SHA-256, an unpinned name, a bad pin and an unknown archive
#     type are refused, leaving no file behind;
#   - mirror-sources.sh copies every pinned archive, writes README.txt,
#     sources.txt and SOURCES-SHA256SUMS.txt (which sha256sum -c accepts),
#     and names the tag it was given;
#   - released-pins.sh finds the release tag with the checkout's pins (in a
#     made-up git repository), and none once they change;
#   - the real sources.txt pins every library build-deps.sh fetches.
# Prints [PASS]/[FAIL] lines; exits with the number of failures.
#
# Usage (needs bash, git, cmp, tar, gzip, awk, sha256sum or shasum): scripts/ios/test-sources.sh
set -uo pipefail

for tool in git cmp tar gzip awk; do
    # Said plainly (the CI's MSYS2 shells install git and diffutils for this test).
    command -v "$tool" >/dev/null 2>&1 || { echo "test-sources.sh: $tool not found (needs bash, git, cmp, tar, gzip, awk)" >&2; exit 1; }
done
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
checks=0
failures=0
result() { # <status> <name>
    checks=$((checks + 1))
    if [ "$1" -eq 0 ]; then echo "[PASS] $2"; else echo "[FAIL] $2"; failures=$((failures + 1)); fi
}
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | awk '{ print $1 }'
}

# Two made-up libraries.
mkdir -p "$work/src/alpha-1.0" "$work/src/beta-2.5" "$work/have" "$work/out" "$work/mirror"
echo "alpha source" > "$work/src/alpha-1.0/README"
echo "beta source" > "$work/src/beta-2.5/README"
tar -czf "$work/have/alpha-1.0.tar.gz" -C "$work/src" alpha-1.0
tar -czf "$work/have/beta-2.5.tar.gz" -C "$work/src" beta-2.5
alpha_sha=$(sha256 "$work/have/alpha-1.0.tar.gz")
beta_sha=$(sha256 "$work/have/beta-2.5.tar.gz")
cat > "$work/sources.txt" <<EOF
# name version sha256 url
alpha 1.0 $alpha_sha https://example.invalid/alpha/archive/v1.0.tar.gz
beta  2.5 $beta_sha  https://example.invalid/beta-2.5.tar.gz
wrong 3.0 0000000000000000000000000000000000000000000000000000000000000000 https://example.invalid/wrong-3.0.tar.gz
short 1.0 abc https://example.invalid/short-1.0.tar.gz
zipped 1.0 $alpha_sha https://example.invalid/zipped-1.0.zip
EOF
cp "$work/have/alpha-1.0.tar.gz" "$work/have/wrong-3.0.tar.gz"
export OPENSHAPE_IOS_SOURCES="$work/sources.txt"
export OPENSHAPE_SOURCES_DIR="$work/have"

path=$(bash "$here/fetch-source.sh" alpha "$work/out" 2>"$work/err")
result $? "fetch-source: a pinned archive is taken $(tr '\n' ' ' < "$work/err")"
[ "$path" = "$work/out/alpha-1.0.tar.gz" ] && [ "$(sha256 "$path")" = "$alpha_sha" ]
result $? "fetch-source: it is named <name>-<version>.<ext> and has the pinned SHA-256 ($path)"
touch -t 202001010000 "$path"
bash "$here/fetch-source.sh" alpha "$work/out" >/dev/null 2>&1 && [ "$(find "$path" -newer "$work/sources.txt" | wc -l)" -eq 0 ]
result $? "fetch-source: an archive already there with the right SHA-256 is kept"

bash "$here/fetch-source.sh" wrong "$work/out" >/dev/null 2>"$work/err"
status=$?
[ "$status" -ne 0 ] && grep -q "SHA-256 mismatch" "$work/err" && [ ! -e "$work/out/wrong-3.0.tar.gz" ] && [ ! -e "$work/out/wrong-3.0.tar.gz.part" ]
result $? "fetch-source: an archive whose SHA-256 differs is refused and removed"
bash "$here/fetch-source.sh" gamma "$work/out" >/dev/null 2>"$work/err"
[ $? -ne 0 ] && grep -q "not pinned" "$work/err"
result $? "fetch-source: an unpinned library is refused"
bash "$here/fetch-source.sh" short "$work/out" >/dev/null 2>"$work/err"
[ $? -ne 0 ] && grep -q "bad SHA-256" "$work/err"
result $? "fetch-source: a pin that is not a SHA-256 is refused"
bash "$here/fetch-source.sh" zipped "$work/out" >/dev/null 2>"$work/err"
[ $? -ne 0 ] && grep -q "unsupported archive type" "$work/err"
result $? "fetch-source: an unknown archive type is refused"

# mirror-sources.sh on the good pins only.
grep -v -E '^(wrong|short|zipped) ' "$work/sources.txt" > "$work/good.txt"
OPENSHAPE_IOS_SOURCES="$work/good.txt" bash "$here/mirror-sources.sh" "$work/mirror" v9.9.9 >"$work/log" 2>&1
result $? "mirror-sources: succeeds $(tail -n 2 "$work/log" | tr '\n' ' ')"
[ -f "$work/mirror/alpha-1.0.tar.gz" ] && [ -f "$work/mirror/beta-2.5.tar.gz" ]
result $? "mirror-sources: every pinned archive is there"
(cd "$work/mirror" && [ "$(wc -l < SOURCES-SHA256SUMS.txt)" -eq 2 ] && \
    if command -v sha256sum >/dev/null 2>&1; then sha256sum -c --status SOURCES-SHA256SUMS.txt; else shasum -a 256 -c --status SOURCES-SHA256SUMS.txt; fi)
result $? "mirror-sources: SOURCES-SHA256SUMS.txt lists both and checks out"
grep -q "tree/v9.9.9" "$work/mirror/README.txt" && grep -q "blob/v9.9.9/BUILDING.md" "$work/mirror/README.txt" \
    && cmp -s "$work/good.txt" "$work/mirror/sources.txt"
result $? "mirror-sources: README.txt names the tag's source and steps; sources.txt is the pins"
OPENSHAPE_IOS_SOURCES="$work/sources.txt" bash "$here/mirror-sources.sh" "$work/mirror2" >/dev/null 2>&1
[ $? -ne 0 ] && [ ! -e "$work/mirror2/SOURCES-SHA256SUMS.txt" ]
result $? "mirror-sources: a bad pin fails the mirror before its checksums are written"

# released-pins.sh: which release tag has this checkout's pins.
unset OPENSHAPE_IOS_SOURCES OPENSHAPE_SOURCES_DIR
repo="$work/repo"
mkdir -p "$repo/scripts/ios"
git_q() { git -C "$repo" -c user.name=test -c user.email=test@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false "$@" >/dev/null 2>&1; }
git_q init -q
echo "old readme" > "$repo/README"
git_q add README && git_q commit -q -m "before the pins" && git_q tag v0.1.0
cp "$work/good.txt" "$repo/scripts/ios/sources.txt"
git_q add scripts && git_q commit -q -m pins && git_q tag -a v0.2.0-beta1 -m beta
out=$(bash "$here/released-pins.sh" "$repo")
[ $? -eq 0 ] && [ "$out" = "v0.2.0-beta1" ]
result $? "released-pins: names the release tag with the same pins ($out)"
echo "gamma 1.0 $alpha_sha https://example.invalid/gamma-1.0.tar.gz" >> "$repo/scripts/ios/sources.txt"
out=$(bash "$here/released-pins.sh" "$repo")
[ $? -eq 1 ] && [ -z "$out" ]
result $? "released-pins: changed pins are on no release (a tag without sources.txt never matches)"
git_q commit -q -a -m "new pins" && git_q tag v0.2.0
out=$(bash "$here/released-pins.sh" "$repo")
[ $? -eq 0 ] && [ "$out" = "v0.2.0" ]
result $? "released-pins: a new tag carries them ($out)"

# The real pins cover what build-deps.sh fetches.
missing=""
for name in $(sed -n 's/^fetch \([a-z0-9]*\) .*/\1/p' "$here/build-deps.sh"); do
    awk -v n="$name" '$1 == n { found = 1 } END { exit !found }' "$here/sources.txt" || missing="$missing $name"
done
[ -z "$missing" ] && [ -n "$(sed -n 's/^fetch \([a-z0-9]*\) .*/\1/p' "$here/build-deps.sh")" ]
result $? "sources.txt pins every library build-deps.sh fetches${missing:+ (missing:$missing)}"

echo "test-sources: $((checks - failures))/$checks passed"
exit "$failures"
