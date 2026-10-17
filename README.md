# Command HQ port

A native port of *Command HQ* (1990, Dan Bunten / Ozark Softscape / MicroProse) for **macOS (Apple Silicon)**, **Linux**, and modern web browsers via **WebAssembly**.

The port statically decompiles and recompiles the original 16-bit real-mode MS-DOS executable (`CHQ.EXE`) and its EGA graphics overlay driver (`EGRAPHIC.EXE`) into portable C++20, running inside a deterministic PC environment that emulates the 1 MB real-mode address space, 320×200 16-color planar EGA graphics (`Mode 0Dh`) with hardware page flipping, Microsoft Mouse (`INT 33h`) callbacks, PC speaker audio (`8253 PIT`), and DOS/BIOS services.

This repository contains **only clean-room engine code, reverse-engineering tools, and build infrastructure**. No copyrighted game binaries, assets, or derived generated C++ code are committed to the repository. The static recompiler processes your legally owned copy of *Command HQ* at build time.

> [!NOTE]
> **Disclaimer**: *Command HQ* is a trademark of MicroProse / Rebellion Developments. This project is an independent, non-commercial educational and digital preservation research project. No copyrighted game binaries, media assets, or decompiled code are distributed in this repository.

---

## Architecture & Design

```
+------------------------------------------------------------------------+
|                          Host Presentation Layer                       |
|   macOS Native: SDL3 + Metal/Cocoa    |    Web: SDL3 + WebGL + IDBFS   |
+------------------------------------------------------------------------+
                                    |
+------------------------------------------------------------------------+
|                             engine (C++20)                             |
|                                                                        |
|  +--------------------+  +--------------------+  +------------------+  |
|  |    dos::Machine    |  |     dos::Ega       |  |   dos::Speaker   |  |
|  | 1 MB real-mode mem |  | 320x200 Mode 0Dh   |  | PIT ch2 square-  |  |
|  | INT 08h/09h/33h    |  | 4-plane EGA VRAM   |  | wave audio synth |  |
|  | Fast block dispatch|  | CRTC page flipping |  | 44.1 kHz stream  |  |
|  +--------------------+  +--------------------+  +------------------+  |
|            |                                                           |
|  +--------------------+  +--------------------+                        |
|  | recompiled segments|  |    Interpreter     |                        |
|  | (36 C++ switch-    |  |  fallback for stack|                        |
|  |  dispatched units) |  |  thunks & overlays |                        |
|  +--------------------+  +--------------------+                        |
+------------------------------------------------------------------------+
                                    ^
                                    | static translation
+------------------------------------------------------------------------+
|                        tools/chqre (Python 3.12)                       |
|   8086 decoder  *  CFG discovery  *  EGRAPHIC.EXE overlay binding      |
+------------------------------------------------------------------------+
                                    ^
                                    | binary analysis
                      Original CHQ.EXE + EGRAPHIC.EXE
```

### Static Recompiler (`tools/chqre`)
The recompiler parses the 16-bit MZ executable (`CHQ.EXE`) and the EGA graphics overlay (`EGRAPHIC.EXE`), resolves relocations, binds the 58-entry `OVR:` graphics jump table at `1786:0A5C`, and performs iterative recursive-descent control-flow discovery across **65,209 instructions in 16,692 basic blocks** with zero decode errors:
- **Overlay binding**: Relocates `EGRAPHIC.EXE` to its runtime segment (`0x29B0`) and links all 56 active graphics driver entry points into the static call graph.
- **No-return runtime procedures**: Stops control-flow traversal after Microsoft C 5.1 `_exit`/`__exit`/`_amsg_exit` routines so trailing data tables are never misdecoded as code.
- **Segment translation**: Emits 36 C++ switch-dispatched translation units (`build/generated/seg_XXXX.cpp`) plus `program.cpp`.

### Emulation Layer (`engine/src/dos`)
- **`chq::dos::Machine`**: Manages the 1 MB real-mode memory map, CPU register state, flags, segment registers, hardware interrupts (`INT 08h` timer, `INT 09h` XT keyboard scancodes via port `60h`), BIOS/DOS interrupts (`INT 10h`, `INT 16h`, `INT 1Ah`, `INT 21h` including `AH=4Bh AL=03h` overlay loader and `AH=4Eh/4Fh` directory enumeration), and Microsoft Mouse (`INT 33h`) callbacks.
- **Manual Copy-Protection Convenience Hook**: Automatically populates the unpacked manual verification word (`DS:0xE73A` -> `DS:0xCA86`) when the startup prompt initializes so pressing `Return` advances directly into the main menu (configurable in macOS Settings).
- **EGA Display (`dos::Ega`)**: Emulates 320×200 16-color Mode `0Dh` planar EGA memory (`A000:0000`), write modes 0–2, ALU latches, CRTC cursor/start-address registers (`3D4h`/`3D5h`) for multi-page flipping, and 4:3 CRT presentation.
- **Audio & PIT Timing (`dos::Speaker`)**: Synthesizes 44.1 kHz square-wave audio from 8253 PIT channel 2 and port `61h` speaker modulation.

---

## Prerequisites & Environment Setup

The repository uses [mise](https://mise.jdx.dev/) for pinned development environments and [Docker](https://www.docker.com/) for containerized Linux CI and WebAssembly builds.

### 1. Install mise Tools
```zsh
mise trust
mise install
```
This provisions Python 3.12, `uv`, CMake, and Ninja.

### 2. Container Engine
Docker (or Colima on macOS Apple Silicon) is used for containerized Linux CI runs, headless Ghidra analysis, and WebAssembly compilation.

---

## Usage Guide

### 1. Game Binary Setup
Place your original *Command HQ* files (`CHQ.EXE`, `EGRAPHIC.EXE`, `FONTS.CHQ`, `TEXT.DAT`, `*.PCX`, `*.WAL`, etc.) in the directory directly above this repository:
```
comhq/
  ├── CHQ.EXE
  ├── EGRAPHIC.EXE
  ├── FONTS.CHQ
  ├── TEXT.DAT
  ├── CHQTITLE.PCX
  ├── MAINMENU.PCX
  ├── CHQMAP.PCX
  └── comhq-port/        <-- this repository
```
*(You can also set the `CHQ_DATA` environment variable to point to any directory containing `CHQ.EXE`.)*

### 2. Static Recompilation
Generate the portable C++ code from `CHQ.EXE` and `EGRAPHIC.EXE`:
```zsh
./scripts/recomp.zsh
```
This analyzes the binaries and writes the generated translation units to `build/generated/`.

### 3. Native macOS Build & Run
Build the native Apple Silicon app:
```zsh
./scripts/build.zsh
```
Run the game natively:
```zsh
./scripts/run.zsh
```

### 4. macOS Application Bundle Packaging
Package a standalone, codesigned macOS application bundle (`Command HQ.app`), zip archive, and drag-and-drop disk image (`Command HQ.dmg`) with a custom macOS Tahoe squircle icon:
```zsh
./scripts/bundle.zsh
```
Output:
- `dist/Command HQ.app`
- `dist/Command HQ.zip`
- `dist/Command HQ.dmg`

To notarize and staple the bundle for distribution (requires Apple Developer credentials):
```zsh
./scripts/notarize.zsh
```

### 5. WebAssembly Browser Build
Build the WebAssembly version in the Emscripten container and serve it locally:
```zsh
./scripts/build-wasm.zsh --serve
```
Open `http://localhost:8080/comhq.html` in any modern web browser.

---

## In-Game Controls & Features

| Key / Mouse | Action |
|---|---|
| **Left-Click** | Select menus, select units, and set movement destinations on the world map |
| **Right-Click** | Inspect city/region production or cancel current unit selection |
| **Return / Space** | Confirm dialogs, pass pre-filled manual verification prompt, advance screens |
| **Escape** | Open in-game options / pause menu |
| **Cmd-N** | Restart game |
| **Cmd-O** | Import a saved game (`CHQ-xx.SG`) into the Saves folder |
| **Cmd-Shift-O** | Reveal Saves folder in Finder |
| **Cmd-,** | Open macOS Settings (Full Screen default, Manual Word auto-fill, Audio Volume/Mute) |
| **Cmd-?** | Open Command HQ Help & original release notes |
| **Cmd-Q** | Quit game |

---

## Development Guide

### Repository Layout

```
engine/                  C++20 game engine and platform shell
  ├── CMakeLists.txt     Build configuration for native and Emscripten targets
  ├── resources/         macOS Info.plist, entitlements, and AppIcon.icns
  ├── src/
  │   ├── app/           Application shell (main.cpp, headless, lockstep, macOS bridge)
  │   ├── core/          Byte reading, MZ image parsing, save validation, asset paths
  │   ├── dos/           PC machine, 8086 interpreter, EGA Mode 0Dh, INT 33h mouse, PIT speaker
  │   └── gfx/           320x200 EGA surface and FONTS.CHQ bitmap font renderer
  ├── web/               HTML/CSS shell page for WebAssembly build
  └── tests/             C++ unit tests (doctest) and Unicorn instruction corpus verifier
tools/
  ├── chqre/             Python 3.12 reverse-engineering & static recompiler toolsuite
  │   └── src/chqre/     8086 decoder, CFG discovery, C++ codegen, overlay/font parsers
  └── ghidra/            Headless Ghidra analysis script (PyGhidra)
docker/
  ├── ci.Dockerfile      Linux CI environment (Clang, SDL3, Unicorn, doctest)
  ├── wasm.Dockerfile    Emscripten WebAssembly build environment
  └── analysis.Dockerfile Ghidra 12 analysis container
scripts/                 macOS zsh build, test, bundle, notarization, and analysis scripts
docs/
  └── findings.md        Technical analysis of CHQ.EXE and EGRAPHIC.EXE
```

### Running Tests

#### 1. Native macOS Test Suite
Runs all 27 Python recompiler tests and all C++ test suites on macOS:
```zsh
./scripts/test.zsh
```

#### 2. Linux Container CI Test Suite
Runs the test suite inside the containerized Linux environment matching GitHub Actions CI:
```zsh
./scripts/test.zsh --docker
```

#### 3. WebAssembly Node.js Test Suite
Builds the WebAssembly binaries and runs the C++ unit test suite under Node.js:
```zsh
./scripts/build-wasm.zsh
```
