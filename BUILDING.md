# Building OpenShape

Only instructions that have actually been run are listed. Verified on
2026-09-25: Windows 11 Pro (26200), no administrator rights.

## Windows (MSYS2 UCRT64) — verified

### 1. Install MSYS2

Either run the official installer from <https://www.msys2.org> (installs to
`C:\msys64`), or — without admin rights, as verified here — extract the base
archive into your profile:

```bash
curl -L -o msys2-base.tar.xz https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.tar.xz
tar -xf msys2-base.tar.xz -C "$USERPROFILE"
```

Start the **MSYS2 UCRT64** shell (`msys64\ucrt64.exe`) once; it initializes
its keyring on first launch.

### 2. Install the toolchain and libraries (UCRT64 shell)

```bash
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-opencascade \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-shadertools mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-gtest mingw-w64-ucrt-x86_64-nlohmann-json \
  mingw-w64-ucrt-x86_64-libzip mingw-w64-ucrt-x86_64-eigen3
```

Verified versions: GCC 16.2.0, CMake 4.4.3, Ninja 1.13.2, OCCT 7.9.3,
Qt 6.11.2, GTest 1.18.0, nlohmann-json 3.12.0, libzip 1.11.4, Eigen 5.0.1.
The PlaneGCS sketch solver is vendored in `third_party/planegcs` and built
from source (as a C++23 shared library) automatically.

### 3. Configure, build, test (UCRT64 shell, in the repository root)

```bash
cmake --preset msys2-ucrt64
cmake --build build/msys2-ucrt64
ctest --test-dir build/msys2-ucrt64 --output-on-failure
```

`ctest` includes `acceptance_gui`, which opens the application window and
drives it with synthetic input for ~15 s (it moves the mouse cursor). On a
machine without a desktop session, skip it:

```bash
ctest --test-dir build/msys2-ucrt64 -LE gui
```

Options: `-DOPENSHAPE_BUILD_APP=OFF` builds only the Qt-free core and its
tests; `-DOPENSHAPE_WARNINGS_AS_ERRORS=ON` makes warnings fatal.

### 4. Run

From the UCRT64 shell (so Qt and OCCT DLLs are on `PATH`):

```bash
./build/msys2-ucrt64/bin/OpenShape.exe
./build/msys2-ucrt64/bin/OpenShape.exe path/to/model.openshape
```

Developer switches:

```bash
./build/msys2-ucrt64/bin/OpenShape.exe --demo bracket --screenshot shot.png
./build/msys2-ucrt64/bin/OpenShape.exe --acceptance out-dir
```

Logs are written to stderr and to
`%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log`.

Building from another shell (e.g. Git Bash) also works if
`<msys64>/ucrt64/bin` is first on `PATH`.

### 5. Package a self-contained folder (verified)

```bash
pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-ntldd
bash scripts/package-windows.sh
```

This produces `dist/OpenShape/` (≈290 MB, 363 files): the stripped
executable, Qt (via `windeployqt6`), OCCT, PlaneGCS and runtime DLLs, Qt
plugins, QML modules, a `qt.conf`, and license files. Verified by running the
packaged `OpenShape.exe` with `PATH` reduced to `C:\Windows\System32` —
including the full `--acceptance` run (60/60). No installer yet.

## Other platforms — not yet verified

The CMake project uses only `find_package` for OCCT (≥ 7.8 recommended),
Qt ≥ 6.8 (Core, Gui, GuiPrivate, Qml, Quick, QuickControls2, ShaderTools),
nlohmann_json, libzip, Eigen3 and GTest, and needs a C++23 compiler for the
vendored solver, so Linux distribution packages and Homebrew
should work with the `linux` preset. This has **not** been run yet.
