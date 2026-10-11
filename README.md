# INVATAXI – portable C / SDL2 port

A reverse-engineered, portable re-implementation of **INVATAXI** (Åkesoft, 1994),
a Finnish DOS mouse game where you drive an "invataxi" and have to understand
where your mumbling passenger wants to go.

The port is a function-by-function translation of the original Turbo Pascal 6
program (disassembled from `INVATAXI.EXE`) to C99, with SDL2 standing in for
VGA, the Crt unit, the INT 33h mouse driver and the Sound Blaster. The MOD
player "PaxiMod" that was linked into the game is re-implemented in
`src/modplayer.c`. See `docs/REVERSE_ENGINEERING.md` for the details.

**You need the original game files.** The port reads them at runtime:
`INVATAXI.EXE`, `HJUVA_#1.EXE`, `HJUVA_#2.EXE`, `HARMONIA.NVA`,
`HISCORE.NVA` and `1.SKP` … `24.SKP`. The pictures are unpacked straight out of
the PKLITE-compressed EXEs, so nothing has to be extracted by hand.

## Play in the browser

**[Play INVATAXI in your browser](https://klassikkopelit.github.io/invataxi/)**,
no installation or game files needed.

## Download

Version 1.0.0:

- [Windows (64-bit)](https://github.com/klassikkopelit/invataxi/releases/download/v1.0.0/invataxi-v1.0.0-windows-x64.zip)
- [macOS (Apple silicon and Intel)](https://github.com/klassikkopelit/invataxi/releases/download/v1.0.0/invataxi-v1.0.0-macos-universal.zip)
- [Linux (64-bit)](https://github.com/klassikkopelit/invataxi/releases/download/v1.0.0/invataxi-v1.0.0-linux-x64.zip)

All versions: [Releases](https://github.com/klassikkopelit/invataxi/releases).
You also need the original INVATAXI game files, see `readme.txt` in the zip.

## Original game files

You can download the original game files from [Internet Archive](https://archive.org/details/invataxi)

## Building

Requirements: a C99 compiler, CMake ≥ 3.10 and SDL2 (2.0.10 or newer).

### Linux
```sh
sudo apt install build-essential cmake libsdl2-dev     # Debian/Ubuntu
# sudo dnf install gcc cmake SDL2-devel                # Fedora
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS
```sh
brew install cmake sdl2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows – Visual Studio
Install SDL2 with vcpkg (`vcpkg install sdl2:x64-windows`) or download
`SDL2-devel-2.x.x-VC.zip` from <https://github.com/libsdl-org/SDL/releases>, then
```bat
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
rem   or: -DSDL2_DIR=C:/SDL2-2.x.x/cmake
cmake --build build --config Release
```
`SDL2.dll` is copied next to `invataxi-sdl.exe` automatically.

### Windows – MinGW / MSYS2
```sh
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-SDL2
cmake -S . -B build -G "MinGW Makefiles" && cmake --build build
```

There is also a plain makefile for systems with `sdl2-config`: `make -f Makefile.unix`.

### Web version (WebAssembly)

Install [Emscripten](https://emscripten.org/docs/getting_started/downloads.html)
(version 6.0.12 is what the GitHub workflow uses), then:
```sh
source ~/emsdk/emsdk_env.sh            # once per terminal window
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
python3 -m http.server -d build-web 8000
```
and open <http://localhost:8000>. The page has to come from a web server;
opening `index.html` straight from disk does not work. The original game
files in `assets/` are packed into the page (`index.data`), and high scores
are kept in the browser's local storage. The web-only code is inside
`#ifdef __EMSCRIPTEN__` blocks and in `web/shell.html`, so the desktop builds
are unchanged.

### Prebuilt releases (GitHub Actions)

`.github/workflows/build-release.yml` builds Windows (x64), macOS (universal)
and Linux (x64) zips on every push to `main` and on pull requests; the zips are
attached to the workflow run as artifacts. Pushing a version tag also publishes
them as a GitHub release:
```sh
git tag v1.0.0
git push origin v1.0.0
```
Each zip holds the executable, `readme.txt` (from `packaging/`), the
libraries it needs (`SDL2.dll` on Windows; on macOS and Linux SDL2 is linked
statically) and an empty `data/` folder for the original game files, which are
never included.

`.github/workflows/web.yml` builds the web version and publishes it on GitHub
Pages on every push to `main` (one-time setup: Settings → Pages → Source:
"GitHub Actions").

## Running

The program is called `invataxi-sdl` so that it can never overwrite the
original `INVATAXI.EXE` (on Windows file names ignore case), which it needs.

Run `invataxi-sdl` inside the folder with the original files, or give the folder
as an argument:
```sh
./build/invataxi-sdl /path/to/invataxi
./build/invataxi-sdl --sound-menu /path/to/invataxi   # with the original sound menu
```
If no argument is given the game looks in the current directory, then next to
the executable, then in a `data` folder next to the executable. So you can
either put the original files into a `data` folder beside `invataxi-sdl.exe`
and `SDL2.dll` (the layout of the release zips), or drop those two files into
the original game folder, and double-click the exe. `HISCORE.NVA` is updated in
place, as in the original.

Controls are those of the original: number keys in the menus, the left mouse
button on the map, any key to stop the taxi, ESC to give up.
`Alt+Enter` or `F11` toggles fullscreen.

## How faithful is it?

* Same screens, texts (in Finnish), colours, layout and timings (everything is
  counted in 70 Hz vertical retraces, like the original).
* The two helper programs `HJUVA_#1.EXE` / `HJUVA_#2.EXE` that the game
  `Exec`s to put a picture on screen are emulated by drawing the same
  picture.
* The original sound card / mixing rate menu is skipped: the game starts
  straight away as if you had picked Sound Blaster at 22000 Hz. Run
  `invataxi-sdl --sound-menu` to get the original menu back (the chosen rate is
  then used, and "4. Ilman ääniä" plays without sound, as in the original).
* All of the original's quirks are kept on purpose: the score is a random
  number, getting on the high-score list is a coin toss, the list position is
  random, and ESC plus a wrong answer can push lives below zero.

Small, deliberate differences:
* Text mode uses the IBM VGA 8×16 font (from VileR's
  [VGA text mode fonts](https://github.com/viler-int10h/vga-text-mode-fonts)).
* The mouse pointer is drawn black/white regardless of the palette.
* Very short clicks that start and end between two frames are not lost.
* If `HISCORE.NVA` is missing, the default table is shown instead of the
  original's "Runtime error 002".
* The farewell text stays on screen for 1.5 s before the window closes.

## Source layout

| File | Purpose |
|---|---|
| `src/game.c` | game logic, translated procedure by procedure (original offsets in comments) |
| `src/modplayer.c` | PaxiMod-compatible ProTracker player + speech/effect channel |
| `src/platform.c` | SDL2: VGA mode 13h / text mode, Crt, keyboard, mouse, timing, audio |
| `src/pklite.c` | PKLITE 1.12 unpacker used to read the pictures from the EXEs |
| `src/files.c` | case-insensitive access to the DOS file names |
| `src/font_vga.h`, `src/cp437.h`, `src/periods.h` | font, code page and period tables |
| `web/shell.html` | the web page around the WebAssembly version |
| `assets/` | the original game files, packed into the web version |
