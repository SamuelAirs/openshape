#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Builds the OpenCASCADE (OCCT) that OpenShape's Windows release ships:
# the same version as MSYS2's package (7.9.3, so the headers match) with
# the same source patches, but WITHOUT FFmpeg, FreeImage, TBB, VTK, Tcl/Tk,
# OpenVR, RapidJSON, Draco and the Draw harness. MSYS2's package links its
# visualization toolkit (which the STEP translator needs) against a GPL
# build of FFmpeg and, through FreeImage/libtiff, jbigkit (GPL-2.0): see
# docs/LICENSING.md and TD-17 in docs/TECHNICAL_DEBT.md.
#
# Only the toolkits OpenShape links (src/geometry/CMakeLists.txt) and
# their dependencies are built; TKService/TKV3d come along because TKXCAF
# (STEP) needs them. FreeType (FTL) stays: TKService uses it for fonts.
# No OpenGL toolkit is built (OpenShape draws with Qt).
#
# Usage (MSYS2 UCRT64 toolchain first in PATH: gcc, cmake, ninja, curl):
#   scripts/windows/build-occt.sh <install-prefix> [jobs]
# The sources and build tree go to $OCCT_WORK_DIR (default: <prefix>-work);
# delete it afterwards to save space. A full build takes 20-40 minutes on
# 6 cores (over an hour on a 4-core CI runner).
#
# Keep in sync: OCCT_TOOLKITS below with src/geometry/CMakeLists.txt, and
# OCCT_VERSION with the MSYS2 package the dev build uses (CI caches the
# result keyed on this file's hash).
set -euo pipefail

PREFIX=${1:?usage: build-occt.sh <install-prefix> [jobs]}
JOBS=${2:-$(nproc)}

OCCT_VERSION=7.9.3
OCCT_TAG=V7_9_3
OCCT_URL="https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/$OCCT_TAG.tar.gz"
OCCT_SHA256=5ecf094ec6b12d5413dfb851d8c3590c354058aee556e32e408bdfbf8c357d57

# The patches MSYS2 applies to its package (mingw-w64-opencascade 7.9.3-3),
# at a fixed commit of github.com/msys2/MINGW-packages, so this build
# behaves like the one the dev build and CI test against. Left out: 0002
# (FFmpeg), 0007/0013 (static builds), 0010 (TBB), 0014 (aarch64).
MSYS2_COMMIT=ee742a85e47399f374334cb4e736fe575cbef999
MSYS2_PATCH_URL="https://raw.githubusercontent.com/msys2/MINGW-packages/$MSYS2_COMMIT/mingw-w64-opencascade"
PATCHES=(
    "0001-do-not-redefine-WIN32_WINNT.patch 018829156bc3fc0f44c600c67cd21f141c0051c1f15fe6bdcf3f3246a19dc141"
    "0003-BRepFill_Filling-WireFromList-We-can-t-assume-that-a.patch 42edcf398e0670012bf2668bd493997a43ab82a24a1c889bfb080e5256776619"
    "0004-BRepFill_Filling-Curve-constraints-confused-by-impli.patch c6f06b012f3ff6b4355151af4981593f87c12ccb2aaaa15c2cd65799f9efc336"
    "0006-BRepOffset_Tool-TryProject-Check-return-of-BRepLib-B.patch c3cdd50ddeace18259341a25041b90ed18e73898161b81242868ae6a53a2c848"
    "0009-SSE2-detection-gcc.patch 226c000886a901d62cc09ca550009e4bbae84933a3b1bbded5c4f9f246734b08"
)

# What OpenShape links (src/geometry/CMakeLists.txt); OCCT's CMake adds
# their dependencies (TKCAF, TKXCAF, TKV3d, TKService, TKHLR, ...).
OCCT_TOOLKITS="TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool \
TKShHealing TKFillet TKOffset TKFeat TKMesh TKDE TKXSBase TKDESTEP TKDESTL"

# DLL names that must never end up in the result (GPL codecs and image libraries).
GPL_PATTERN='avcodec|avformat|avutil|swscale|swresample|postproc|avfilter|avdevice|x264|x265|freeimage|jbig|tiff'

for tool in gcc cmake ninja curl sha256sum tar patch cygpath ntldd; do
    command -v "$tool" >/dev/null || { echo "build-occt.sh: '$tool' not found (MSYS2 UCRT64 toolchain first in PATH?)"; exit 1; }
done
MINGW_PREFIX=$(cygpath -m "$(dirname "$(dirname "$(command -v gcc)")")") # e.g. C:/msys64/ucrt64
[ -f "$MINGW_PREFIX/lib/libfreetype.dll.a" ] || { echo "FreeType not found in $MINGW_PREFIX (pacman -S mingw-w64-ucrt-x86_64-freetype)"; exit 1; }

mkdir -p "$PREFIX"
PREFIX=$(cygpath -m "$(cd "$PREFIX" && pwd)")
WORK=${OCCT_WORK_DIR:-${PREFIX%/}-work}
mkdir -p "$WORK"
WORK=$(cygpath -m "$(cd "$WORK" && pwd)")

# download <url> <file> <sha256>: fetch once, always verify.
download() {
    if [ ! -f "$2" ] || ! echo "$3  $2" | sha256sum -c --status; then
        echo "== downloading $1"
        curl -fsSL --retry 3 -o "$2.part" "$1"
        mv "$2.part" "$2"
    fi
    echo "$3  $2" | sha256sum -c --status || { echo "SHA-256 mismatch for $2 (from $1)"; exit 1; }
}

cd "$WORK"
download "$OCCT_URL" "occt-$OCCT_TAG.tar.gz" "$OCCT_SHA256"
mkdir -p patches
for entry in "${PATCHES[@]}"; do
    name=${entry% *}
    download "$MSYS2_PATCH_URL/$name" "patches/$name" "${entry#* }"
done

# Unpack and patch once per source and patch set, configure once per set of
# options: a rerun (e.g. after a failed verification) only rebuilds what
# changed instead of all of OCCT.
source_stamp="$OCCT_SHA256 ${PATCHES[*]}"
if [ "$(cat src/.openshape-stamp 2>/dev/null)" != "$source_stamp" ]; then
    echo "== unpacking and patching"
    rm -rf src build
    mkdir src
    tar -xzf "occt-$OCCT_TAG.tar.gz" -C src --strip-components=1
    for entry in "${PATCHES[@]}"; do
        patch -d src -Np1 --no-backup-if-mismatch -i "../patches/${entry% *}"
    done
    echo "$source_stamp" > src/.openshape-stamp
fi

modules=(FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange DETools Draw)
config=(
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_LIBRARY_TYPE=Shared
    -DINSTALL_DIR="$PREFIX"
    -DINSTALL_DIR_LAYOUT=Unix
    "-DBUILD_ADDITIONAL_TOOLKITS=$OCCT_TOOLKITS"
    -DBUILD_DOC_Overview=OFF -DBUILD_SAMPLES_QT=OFF -DBUILD_Inspector=OFF
    -DUSE_FFMPEG=OFF -DUSE_FREEIMAGE=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF
    -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_OPENVR=OFF -DUSE_TK=OFF
    -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF -DUSE_EIGEN=OFF
    -DUSE_FREETYPE=ON
    -D3RDPARTY_DIR="$MINGW_PREFIX"
    -D3RDPARTY_FREETYPE_DIR="$MINGW_PREFIX"
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_ft2build="$MINGW_PREFIX/include/freetype2"
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_freetype2="$MINGW_PREFIX/include/freetype2"
    -D3RDPARTY_FREETYPE_DLL_DIR="$MINGW_PREFIX/bin"
    -D3RDPARTY_FREETYPE_DLL="$MINGW_PREFIX/bin/libfreetype-6.dll"
    -D3RDPARTY_FREETYPE_LIBRARY_DIR="$MINGW_PREFIX/lib"
    -D3RDPARTY_FREETYPE_LIBRARY="$MINGW_PREFIX/lib/libfreetype.dll.a"
)
for module in "${modules[@]}"; do
    config+=("-DBUILD_MODULE_$module=OFF") # only BUILD_ADDITIONAL_TOOLKITS and their dependencies
done
if [ "$(cat build/.openshape-stamp 2>/dev/null)" != "${config[*]}" ] || [ ! -f build/build.ninja ]; then
    echo "== configuring"
    rm -rf build
    cmake -G Ninja -S src -B build "${config[@]}"
    echo "${config[*]}" > build/.openshape-stamp
fi

echo "== building with $JOBS jobs"
cmake --build build -j "$JOBS"
rm -rf "$PREFIX"/{bin,lib,include,share}
cmake --install build

# The license texts (the package script copies them into the release).
mkdir -p "$PREFIX/share/licenses/opencascade"
cp src/LICENSE_LGPL_21.txt src/OCCT_LGPL_EXCEPTION.txt "$PREFIX/share/licenses/opencascade/"
# OCCT's environment batch files are not needed.
rm -f "$PREFIX"/bin/*.bat
{
    echo "OpenCASCADE $OCCT_VERSION built for OpenShape by scripts/windows/build-occt.sh"
    echo "Source:  $OCCT_URL"
    echo "SHA-256: $OCCT_SHA256"
    echo "Patches: $MSYS2_PATCH_URL/"
    for entry in "${PATCHES[@]}"; do echo "         ${entry% *}"; done
    echo "Toolkits: $(cd "$PREFIX/bin" && ls libTK*.dll | sed -e 's/^lib//' -e 's/\.dll$//' | tr '\n' ' ')"
    echo "Compiler: $(gcc --version | head -n 1)"
    echo "Options: shared, Release; FreeType on; FFmpeg, FreeImage, TBB, VTK, Tcl/Tk, OpenGL, Draw off"
} > "$PREFIX/BUILD-INFO.txt"

echo "== verifying"
# Every toolkit OpenShape links exists, and no OpenGL toolkit was built.
# (MinGW names them lib<toolkit>.dll.)
for tk in $OCCT_TOOLKITS; do
    [ -f "$PREFIX/bin/lib$tk.dll" ] || { echo "missing $PREFIX/bin/lib$tk.dll"; exit 1; }
done
[ ! -e "$PREFIX/bin/libTKOpenGl.dll" ] || { echo "libTKOpenGl.dll was built (not wanted)"; exit 1; }
# The STEP translator's full dependency tree resolves our toolkits (not
# MSYS2's) and contains no GPL library.
deps=$(PATH="$(cygpath -u "$PREFIX/bin"):$PATH" ntldd -R "$PREFIX/bin/libTKDESTEP.dll")
if grep -iE "$GPL_PATTERN" <<<"$deps"; then
    echo "libTKDESTEP.dll depends on a GPL library (above)"; exit 1
fi
prefix_win=$(cygpath -w "$PREFIX" | tr '\\' '/')
if grep -iE '^\s*libTK[A-Za-z0-9]+\.dll' <<<"$deps" | tr '\\' '/' | grep -viF "$prefix_win/bin/"; then
    echo "libTKDESTEP.dll resolved OCCT toolkits outside $PREFIX (above)"; exit 1
fi
echo "libTKDESTEP.dll dependencies:"
awk '{print "  " $1}' <<<"$deps" | sort -u
echo "== done: $PREFIX ($(du -sh "$PREFIX" | cut -f1)); work dir $WORK ($(du -sh "$WORK" | cut -f1))"
