#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Builds OpenShape's libraries for iPadOS (static, arm64) into one prefix:
# FreeType, OpenCASCADE (no Draw, no OpenGL: OpenShape draws with Qt),
# libzip (zlib comes with the iOS SDK), nlohmann/json and Eigen (headers).
# The versions match the Windows and Homebrew builds. OpenCASCADE is the
# long step (half an hour or more).
#
# Usage (macOS with Xcode, CMake and Ninja):
#   scripts/ios/build-deps.sh <install-prefix> [work-dir]
# The work dir (sources and build trees) defaults to build/ios-deps.
set -euo pipefail

PREFIX=${1:?usage: build-deps.sh <install-prefix> [work-dir]}
WORK=${2:-build/ios-deps}
IOS_MIN=${IOS_DEPLOYMENT_TARGET:-16.0}

OCCT_TAG=V7_9_3
FREETYPE_TAG=VER-2-14-3
LIBZIP_VERSION=1.11.4
JSON_VERSION=3.12.0
EIGEN_VERSION=5.0.1

mkdir -p "$PREFIX" "$WORK"
PREFIX=$(cd "$PREFIX" && pwd)
WORK=$(cd "$WORK" && pwd)

# fetch <url> <dir>: download and unpack a source archive once.
fetch() {
    if [ ! -d "$WORK/$2" ]; then
        echo "== downloading $1"
        mkdir -p "$WORK/$2.tmp"
        curl -fsSL --retry 3 "$1" | tar -xz -C "$WORK/$2.tmp" --strip-components=1
        mv "$WORK/$2.tmp" "$WORK/$2"
    fi
}

IOS=(-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
     -DCMAKE_OSX_DEPLOYMENT_TARGET="$IOS_MIN" -DCMAKE_BUILD_TYPE=Release
     -DCMAKE_INSTALL_PREFIX="$PREFIX")

fetch "https://github.com/freetype/freetype/archive/refs/tags/$FREETYPE_TAG.tar.gz" freetype
fetch "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/$OCCT_TAG.tar.gz" occt
fetch "https://github.com/nih-at/libzip/releases/download/v$LIBZIP_VERSION/libzip-$LIBZIP_VERSION.tar.gz" libzip
fetch "https://github.com/nlohmann/json/archive/refs/tags/v$JSON_VERSION.tar.gz" json
fetch "https://gitlab.com/libeigen/eigen/-/archive/$EIGEN_VERSION/eigen-$EIGEN_VERSION.tar.gz" eigen

echo "== FreeType"
cmake -S "$WORK/freetype" -B "$WORK/build-freetype" "${IOS[@]}" -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build "$WORK/build-freetype" --target install

echo "== OpenCASCADE"
cmake -S "$WORK/occt" -B "$WORK/build-occt" "${IOS[@]}" \
    -DBUILD_LIBRARY_TYPE=Static -DINSTALL_DIR="$PREFIX" \
    -DBUILD_MODULE_Draw=OFF -DBUILD_MODULE_DETools=OFF -DBUILD_DOC_Overview=OFF \
    -DUSE_TK=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF \
    -DUSE_FREETYPE=ON -D3RDPARTY_FREETYPE_DIR="$PREFIX" \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_ft2build="$PREFIX/include/freetype2" \
    -D3RDPARTY_FREETYPE_INCLUDE_DIR_freetype2="$PREFIX/include/freetype2" \
    -D3RDPARTY_FREETYPE_LIBRARY_DIR="$PREFIX/lib" \
    -D3RDPARTY_FREETYPE_LIBRARY="$PREFIX/lib/libfreetype.a" \
    -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF \
    -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_OPENVR=OFF
cmake --build "$WORK/build-occt" --target install

echo "== libzip"
cmake -S "$WORK/libzip" -B "$WORK/build-libzip" "${IOS[@]}" -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF \
    -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
    -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF
cmake --build "$WORK/build-libzip" --target install

# Header-only: installed with the host toolchain (nothing is compiled).
echo "== nlohmann/json, Eigen"
cmake -S "$WORK/json" -B "$WORK/build-json" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DJSON_BuildTests=OFF -DJSON_Install=ON
cmake --build "$WORK/build-json" --target install
cmake -S "$WORK/eigen" -B "$WORK/build-eigen" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_TESTING=OFF -DEIGEN_BUILD_TESTING=OFF -DEIGEN_BUILD_DOC=OFF \
    -DEIGEN_BUILD_BLAS=OFF -DEIGEN_BUILD_LAPACK=OFF -DEIGEN_BUILD_PKGCONFIG=OFF
cmake --build "$WORK/build-eigen" --target install

echo "== done: $PREFIX"
du -sh "$PREFIX"
