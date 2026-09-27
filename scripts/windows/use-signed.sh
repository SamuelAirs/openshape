#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Puts a file that SignPath signed in place of the unsigned one, after
# checking it (.github/workflows/release.yml, docs/CODE_SIGNING.md):
#   1. it is the unsigned file plus an Authenticode signature and nothing
#      else (scripts/windows/pe-signature.py: byte for byte, apart from the
#      signature and the two header fields signing changes);
#   2. Windows accepts the signature: for release-signing it must be
#      "Valid" (a trusted certificate, the file unchanged since signing);
#      test-signing uses SignPath's test certificate, which Windows does not
#      trust, so there it must only be present and match the file;
#   3. then the signed file replaces the unsigned one, and if a
#      SHA256SUMS.txt next to it lists the file, its line is updated and
#      the whole list checked again.
# Prints "Signed: <file> (<policy>): <signer>; <status>" for the notice.
#
# Usage (repository root; MSYS2's usr/bin and Python on PATH):
#   scripts/windows/use-signed.sh <signed-file> <unsigned-file> <release-signing|test-signing>
set -euo pipefail

signed=${1:?usage: use-signed.sh <signed-file> <unsigned-file> <policy>}
target=${2:?usage: use-signed.sh <signed-file> <unsigned-file> <policy>}
policy=${3:?usage: use-signed.sh <signed-file> <unsigned-file> <policy>}
root=$(cd "$(dirname "$0")/../.." && pwd)

[ -f "$signed" ] || { echo "use-signed: SignPath returned no $(basename "$target") (expected $signed)"; exit 1; }
[ -f "$target" ] || { echo "use-signed: $target does not exist"; exit 1; }
case "$policy" in release-signing|test-signing) ;; *) echo "use-signed: unknown signing policy '$policy'"; exit 1 ;; esac

# 1. The same program, only signed.
python "$root/scripts/windows/pe-signature.py" compare "$target" "$signed"

# 2. What Windows says about the signature. Get-AuthenticodeSignature:
#    Valid, or e.g. UnknownError/NotTrusted (certificate not trusted),
#    HashMismatch (file changed after signing), NotSigned.
status=$(SIGNED_FILE=$(cygpath -w "$signed") powershell.exe -NoProfile -NonInteractive -Command \
    '$s = Get-AuthenticodeSignature -LiteralPath $env:SIGNED_FILE; "{0}|{1}|{2}" -f $s.Status, $s.SignerCertificate.Subject, $s.TimeStamperCertificate.Subject' \
    | tr -d '\r')
result=${status%%|*}
rest=${status#*|}
signer=${rest%%|*}
timestamp=${rest#*|}
echo "Signature: $result; signer: ${signer:-none}; timestamp: ${timestamp:-none}"
[ -n "$signer" ] || { echo "use-signed: $signed has no signer certificate"; exit 1; }
case "$result" in
    Valid) ;;
    NotSigned|HashMismatch|NotSupportedFileFormat|Incompatible)
        echo "use-signed: Windows rejects the signature of $signed ($result)"; exit 1 ;;
    *)
        if [ "$policy" = release-signing ]; then
            echo "use-signed: the release signature of $signed is not trusted by Windows ($result)"; exit 1
        fi ;;
esac

# 3. In place of the unsigned file; the checksum list follows.
cp -f "$signed" "$target"
dir=$(dirname "$target")
name=$(basename "$target")
sums="$dir/SHA256SUMS.txt"
# sha256sum lines: 64 hex digits, a space, a mode character (' ' or '*'), the name.
if [ -f "$sums" ] && awk -v n="$name" 'substr($0, 67) == n { found = 1 } END { exit !found }' "$sums"; then
    hash=$(sha256sum "$target" | cut -c1-64)
    awk -v n="$name" -v h="$hash" '{ if (substr($0, 67) == n) print h substr($0, 65, 2) n; else print }' "$sums" > "$sums.new"
    mv -f "$sums.new" "$sums"
    (cd "$dir" && sha256sum -c SHA256SUMS.txt)
fi
short=${signer#CN=}
short=${short%%,*}
echo "Signed: $name ($policy): $short; $result"
