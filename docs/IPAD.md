# Running OpenShape on an iPad

**Status (2026-09-25): prepared on Windows, not yet built on a Mac.** Every
command below still has to be run for real; expect a few fixes on the way.
The easiest route: open Claude Code on the Mac in this repository and ask it
to "build OpenShape for my iPad following docs/IPAD.md" — it can run the
steps and fix what comes up.

What is ready:

- Touch and pen: gestures (one finger, two-finger pan/pinch, two-/three-finger
  tap = undo/redo), pen mode (pen selects and draws, fingers only navigate)
  with a Pen switch, touch-sized controls (44 pt) that are on from the start
  on a tablet, a tool palette that scrolls. Try it on Windows with
  `OpenShape.exe --touch`.
- Build settings for Apple platforms: warning flags Clang accepts, the
  PlaneGCS solver built as a static library on iOS, an app bundle with an
  iPad `Info.plist` (`src/app/ios/Info.plist.in`: iPad only, all
  orientations, projects visible in the Files app).
- A `macos` CMake preset and a macOS CI job (Apple Clang, Homebrew packages).
  The job runs once the GitHub repository is public (it is skipped on a
  private repository, where macOS minutes are expensive).

## What you need

- A Mac with **Xcode** (free, App Store) and its command line tools
  (`xcode-select --install`).
- **Homebrew** (<https://brew.sh>).
- **Qt 6.8 or newer for macOS and iOS**: the Qt Online Installer
  (<https://www.qt.io/download-open-source>, free Qt account) with the
  "macOS" and "iOS" components of one Qt version. (Alternative:
  `pip install aqtinstall`, then `aqt install-qt mac desktop 6.8.3` and
  `aqt install-qt mac ios 6.8.3`.)
- Your **Apple ID** (free) for signing, and the iPad with a cable.

## 1. The desktop app on the Mac (checks the code on Apple's compiler)

```bash
brew install cmake ninja opencascade qt nlohmann-json libzip eigen googletest
cmake --preset macos
cmake --build build/macos
ctest --test-dir build/macos -LE gui
open build/macos/bin/OpenShape.app
```

## 2. The libraries, built for iOS (static, arm64)

Homebrew's libraries are for macOS; iOS needs its own builds of
OpenCASCADE (with FreeType, which its STEP translator pulls in) and libzip.
Install everything into one folder:

```bash
export DEPS=$HOME/openshape-ios-deps
IOS="-G Ninja -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 -DCMAKE_BUILD_TYPE=Release"
```

Sources: OpenCASCADE 7.9.x (<https://github.com/Open-Cascade-SAS/OCCT>, tag
`V7_9_x`), FreeType (<https://gitlab.freedesktop.org/freetype/freetype>),
libzip (<https://libzip.org>), nlohmann/json and Eigen (header-only).

```bash
# FreeType
cmake -S freetype -B build-freetype $IOS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$DEPS \
  -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build build-freetype --target install

# OpenCASCADE (no Draw, no OpenGL: OpenShape draws with Qt)
cmake -S OCCT -B build-occt $IOS -DBUILD_LIBRARY_TYPE=Static -DINSTALL_DIR=$DEPS \
  -DBUILD_MODULE_Draw=OFF -DUSE_TK=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF \
  -DUSE_FREETYPE=ON -D3RDPARTY_FREETYPE_DIR=$DEPS \
  -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF
cmake --build build-occt --target install

# libzip (zlib comes with the iOS SDK)
cmake -S libzip -B build-libzip $IOS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$DEPS \
  -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF \
  -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
  -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF
cmake --build build-libzip --target install

# Header-only libraries
cmake -S json -B build-json -DJSON_BuildTests=OFF -DCMAKE_INSTALL_PREFIX=$DEPS && cmake --build build-json --target install
cmake -S eigen -B build-eigen -DCMAKE_INSTALL_PREFIX=$DEPS && cmake --build build-eigen --target install
```

OpenCASCADE also ships its own iOS script (`adm/scripts/ios_build.sh`) if
the commands above need adjusting. OpenCASCADE is the long step (about an
hour).

## 3. OpenShape for iOS, in Xcode

```bash
~/Qt/6.8.3/ios/bin/qt-cmake -S . -B build/ios -G Xcode \
  -DOPENSHAPE_BUILD_TESTS=OFF -DCMAKE_PREFIX_PATH=$DEPS -DCMAKE_FIND_ROOT_PATH=$DEPS
open build/ios/OpenShape.xcodeproj
```

In Xcode: choose the `openshape` scheme and your iPad as the destination.
Under the target's **Signing & Capabilities**, pick your **Personal Team**
(your Apple ID). If the bundle identifier `io.github.samuelairs.openshape`
is refused, change it (e.g. add your name). Press **Run**.

On the iPad, the first time: **Settings → Privacy & Security → Developer
Mode** (turn on, restart). After the first install: **Settings → General →
VPN & Device Management** → trust your developer certificate.

With a free Apple ID the app stops opening after 7 days: run it from Xcode
again. The paid Apple Developer Program ($99/year) makes that a year and
allows TestFlight and the App Store.

## Expected rough edges

- Open and Save: projects live in the app's Documents folder, which the
  Files app shows under "On My iPad → OpenShape". How Qt's file dialogs
  behave on iPadOS needs testing.
- No hover highlight (touch has no hover; Apple Pencil hover is not used yet).
- Typing values: tap the value field for the on-screen keyboard (typing
  without tapping needs a hardware keyboard).
- Not measured yet: speed and memory with bigger parts.

## Licenses

For your own iPad there is nothing to do. For the App Store see
docs/LICENSING.md: MPL-2.0 allows it; the LGPL parts (Qt, OCCT, PlaneGCS)
are linked statically on iOS, so their object files must be offered for
relinking.

## What to test on the iPad

1. Launch: the model view, toolbars and the axis marker appear; controls are
   comfortably tappable.
2. One finger: tap a face (selects), tap empty space (clears), drag empty
   space (orbits), double-tap a body (selects it).
3. Two fingers: pan and pinch-zoom; a quick two-finger tap undoes, three
   fingers redo.
4. Box → tap the top face → drag the arrow; tap the value field and type a
   height → Enter.
5. Sketch: draw a rectangle with a finger, then with the Pencil; turn on
   **Pen** and check that a resting hand does not draw or select.
6. Extrude a sketch profile; fillet an edge; save; close; reopen from the
   Files app.
7. Rotate the iPad: the layout follows; the tool palette scrolls when short.
8. Note anything slow, hard to hit, or missing — with a screenshot.
