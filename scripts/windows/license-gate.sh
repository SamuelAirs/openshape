#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# License gate for a packaged Windows folder (scripts/package-windows.sh):
# fails when the package contains anything GPL-licensed, so a release can
# never ship GPL code by accident (docs/LICENSING.md, TD-17).
#
# Every executable and DLL must be traced to its origin (by content):
#   - OpenShape.exe and libplanegcs.dll: OpenShape's build (MPL-2.0; PlaneGCS LGPL-2.1+)
#   - OCCT's toolkits from <own-occt-prefix>/bin (scripts/windows/build-occt.sh)
#   - a file of an MSYS2 package (ucrt64/bin, Qt's plugin and QML folders),
#     whose license field (pacman) is then checked:
#       * any OR-alternative free of GPL terms passes (FreeType: FTL OR GPL),
#       * only GPL/AGPL terms fails (LGPL is fine),
#       * GPL mixed with other terms (the package's tools or docs are GPL,
#         its libraries are not) passes only if listed in REVIEWED below.
#   The GCC runtime (GPL-3.0 with the GCC Runtime Library Exception) is
#   allowed explicitly by DLL name. Anything of unknown origin fails.
# Independently, known GPL library names fail wherever they appear.
#
# Usage: scripts/windows/license-gate.sh <package-dir> [own-occt-prefix]
# Prints a report; exit status 0 = pass, 1 = fail.
set -euo pipefail

PKG=${1:?usage: license-gate.sh <package-dir> [own-occt-prefix]}
OWN_OCCT=${2:-}

# Known GPL libraries (FFmpeg built with --enable-gpl, its GPL codecs,
# FreeImage's GPL option and jbigkit).
GPL_NAMES='avcodec|avformat|avutil|avfilter|avdevice|swscale|swresample|postproc|x264|x265|xvidcore|freeimage|jbig'

# The GCC runtime: GPL-3.0-or-later WITH GCC-exception-3.1, which lets
# programs of any license use it.
GCC_RUNTIME='libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll'

# Packages whose license field mixes GPL with other terms, reviewed: what we
# bundle from them is not GPL. "<package without prefix>: reason".
REVIEWED=(
    "qt6-base: Qt libraries and plugins are LGPL-3.0; GPL terms cover build tools"
    "qt6-declarative: Qt Quick/QML libraries and modules are LGPL-3.0; GPL terms cover tools"
    "qt6-svg: Qt SVG library and plugins are LGPL-3.0; GPL terms cover tools"
    "gettext-runtime: libintl is LGPL-2.1-or-later; GPL covers the gettext programs"
    "xz: liblzma is 0BSD; GPL/LGPL cover the command-line scripts"
    "libiconv: libiconv is LGPL-2.1-or-later; GPL covers the documentation and iconv program"
    "gmp: GMP is dual-licensed LGPL-3.0-or-later or GPL-2.0-or-later; used under LGPL-3.0"
)

command -v pacman >/dev/null || { echo "license-gate: pacman not found (MSYS2's usr/bin in PATH?)"; exit 1; }
command -v cygpath >/dev/null || { echo "license-gate: cygpath not found"; exit 1; }
[ -d "$PKG" ] || { echo "license-gate: $PKG is not a folder"; exit 1; }

bin_dir=$(cygpath -m "$(dirname "$(command -v ntldd || command -v gcc)")") # <msys2>/ucrt64/bin
prefix_dir=${bin_dir%/*}
msys_root=${prefix_dir%/*} # pacman prints paths relative to it (/ucrt64/bin/...)
plugins_dir="$prefix_dir/share/qt6/plugins"
qml_dir="$prefix_dir/share/qt6/qml"
own_bin=""
[ -n "$OWN_OCCT" ] && own_bin=$(cygpath -m "$OWN_OCCT/bin")

failures=()
fail() { failures+=("$1"); }

# 1. Known GPL names, anywhere in the package.
while read -r file; do
    fail "known GPL library: $file"
done < <(cd "$PKG" && find . -type f | sed 's|^\./||' | grep -iE "(^|/)[^/]*($GPL_NAMES)[^/]*$" || true)

# 2. Origin of every binary.
declare -A origin    # package-relative path -> source file (MSYS2) or a label
msys_sources=()
mapfile -t binaries < <(cd "$PKG" && find . -type f \( -iname '*.dll' -o -iname '*.exe' \) | sed 's|^\./||' | sort)
for rel in "${binaries[@]}"; do
    name=${rel##*/}
    case "$rel" in
        OpenShape.exe|libplanegcs.dll) origin[$rel]="OpenShape build"; continue ;;
    esac
    if [ -n "$own_bin" ] && [ "$rel" = "$name" ] && [ -f "$own_bin/$name" ]; then
        if cmp -s "$PKG/$rel" "$own_bin/$name"; then origin[$rel]="OCCT (own build)"; continue; fi
        fail "$rel differs from $own_bin/$name"; continue
    fi
    if [ "$rel" = "$name" ]; then
        candidate="$bin_dir/$name"
    elif [ "${rel%%/*}" = "qml" ]; then
        candidate="$qml_dir/${rel#qml/}"
    else
        candidate="$plugins_dir/$rel"
    fi
    if [ -f "$candidate" ] && cmp -s "$PKG/$rel" "$candidate"; then
        origin[$rel]="$candidate"
        msys_sources+=("$candidate")
    else
        fail "unknown origin: $rel (not OpenShape's, not OCCT's own build, not identical to $candidate)"
    fi
done

# 3. Owning MSYS2 packages and their licenses (one pacman call each).
declare -A owner     # source file -> package
if [ ${#msys_sources[@]} -gt 0 ]; then
    while read -r line; do
        # "/ucrt64/bin/<name> is owned by <package> <version>"
        file=${line% is owned by *}
        rest=${line##* is owned by }
        owner["$msys_root$file"]=${rest%% *}
    done < <(pacman -Qo "${msys_sources[@]}" 2>/dev/null </dev/null || true)
fi
mapfile -t packages < <(printf '%s\n' "${owner[@]}" | sed '/^$/d' | sort -u)
declare -A license
if [ ${#packages[@]} -gt 0 ]; then
    while IFS=$'\t' read -r name lic; do
        license[$name]=$lic
    done < <(pacman -Qi "${packages[@]}" </dev/null | awk '/^Name *:/ { n = $3 } /^Licenses *:/ { sub(/^Licenses *: */, ""); print n "\t" $0 }')
fi

is_gpl_term() { # an SPDX id or old-style name: GPL/AGPL family, not LGPL
    local term=${1#spdx:}
    term=${term%% WITH *}
    [[ $term =~ ^A?GPL ]]
}

# classify <license field>: prints free | gpl | mixed
classify() {
    local field=$1 entries=() alternatives=() alt term verdict="" has_gpl has_other
    # MSYS2 separates independent entries with two spaces (sometimes a
    # comma); each covers part of the package: treat them as AND.
    # Documentation-only entries do not matter here.
    field=${field//,/  }
    local joined=""
    while read -r entry; do
        [ -z "$entry" ] && continue
        case "$entry" in documentation:*) continue ;; esac
        entry=${entry//spdx:/}
        joined+="${joined:+ AND }$entry"
    done < <(sed 's/  \+/\n/g' <<<"$field")
    joined=${joined//(/}
    joined=${joined//)/}
    # SPDX: AND binds tighter than OR.
    mapfile -t alternatives < <(sed 's/ OR /\n/g' <<<"$joined")
    local any_free=0 all_gpl=1
    for alt in "${alternatives[@]}"; do
        has_gpl=0 has_other=0
        while read -r term; do
            [ -z "$term" ] && continue
            # Old-style lists without SPDX operators ("GPL3 LGPL"): split on spaces.
            for word in $(sed 's/ WITH [^ ]*//g' <<<"$term"); do
                if is_gpl_term "$word"; then has_gpl=1; else has_other=1; fi
            done
        done < <(sed 's/ AND /\n/g' <<<"$alt")
        [ $has_gpl -eq 0 ] && any_free=1
        [ $has_other -eq 1 ] && all_gpl=0
    done
    if [ $any_free -eq 1 ]; then echo free
    elif [ $all_gpl -eq 1 ]; then echo gpl
    else echo mixed
    fi
}

reviewed_reason() { # <package without prefix>
    local entry
    for entry in "${REVIEWED[@]}"; do
        [ "${entry%%:*}" = "$1" ] && { echo "${entry#*: }"; return 0; }
    done
    return 1
}

declare -A verdict_of
reviewed_used=()
for pkg in "${packages[@]}"; do
    verdict_of[$pkg]=$(classify "${license[$pkg]:-unknown}")
done
for rel in "${binaries[@]}"; do
    src=${origin[$rel]:-}
    case "$src" in ''|"OpenShape build"|"OCCT (own build)") continue ;; esac
    pkg=${owner[$src]:-}
    name=${rel##*/}
    if [ -z "$pkg" ]; then
        fail "$rel: no MSYS2 package owns $src"
        continue
    fi
    if [[ " $GCC_RUNTIME " == *" $name "* ]]; then
        continue
    fi
    short=${pkg#mingw-w64-ucrt-x86_64-}
    case "${verdict_of[$pkg]}" in
        free) ;;
        gpl) fail "$rel: package $pkg is GPL-licensed (${license[$pkg]})" ;;
        mixed)
            if reason=$(reviewed_reason "$short"); then
                [[ " ${reviewed_used[*]} " == *" $short "* ]] || reviewed_used+=("$short")
            else
                fail "$rel: package $pkg mixes GPL with other terms (${license[$pkg]}); review it and add it to REVIEWED in scripts/windows/license-gate.sh if what we bundle is not GPL"
            fi
            ;;
    esac
done

own_count=0
for rel in "${binaries[@]}"; do
    case "${origin[$rel]:-}" in "OpenShape build"|"OCCT (own build)") own_count=$((own_count + 1)) ;; esac
done
echo "License gate: ${#binaries[@]} binaries: $own_count from OpenShape's and OCCT's own builds, the rest from ${#packages[@]} MSYS2 packages"
for pkg in "${packages[@]}"; do
    printf '  %-48s %-6s %s\n' "${pkg#mingw-w64-ucrt-x86_64-}" "${verdict_of[$pkg]}" "${license[$pkg]}"
done
for short in "${reviewed_used[@]}"; do
    echo "  reviewed: $short - $(reviewed_reason "$short")"
done
if [ ${#failures[@]} -gt 0 ]; then
    printf '  FAIL: %s\n' "${failures[@]}"
    echo "License gate: FAIL (${#failures[@]} problems)"
    exit 1
fi
echo "License gate: PASS (no GPL-licensed files)"
