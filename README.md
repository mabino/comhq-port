# Command HQ port

A native port of *Command HQ* (1990, Dan Bunten / Ozark Softscape / MicroProse) for **macOS (Apple Silicon)**, **Linux**, and modern web browsers via **WebAssembly**.

The port statically decompiles and recompiles the original 16-bit real-mode MS-DOS executable (`CHQ.EXE`) and its EGA graphics overlay driver (`EGRAPHIC.EXE`) into portable C++20, running inside a deterministic PC environment that emulates the 1 MB real-mode address space, 320×200 16-color planar EGA graphics (`Mode 0Dh`) with hardware page flipping, Microsoft Mouse (`INT 33h`) callbacks, PC speaker audio (`8253 PIT`), virtual 16550 UART serial modem emulation, and modern internet multiplayer abstraction with NAT traversal.

This repository contains **only clean-room engine code, reverse-engineering tools, and build infrastructure**. No copyrighted game binaries, media assets, or derived generated C++ code are committed to the repository. The static recompiler processes your legally owned copy of *Command HQ* at build time.

> [!NOTE]
> **Disclaimer**: *Command HQ* is a trademark of MicroProse / Rebellion Developments. This project is an independent, non-commercial educational and digital preservation research project. No copyrighted game binaries, media assets, or decompiled code are distributed in this repository.

---

## Architecture & Design

```
+----------------------------------------------------------------------------------------+
|                                Host Presentation Layer                                 |
|      macOS Native: SDL3 + Cocoa/Metal      |         Web: SDL3 + WebGL + IDBFS         |
+----------------------------------------------------------------------------------------+
                                            |
+----------------------------------------------------------------------------------------+
|                                     engine (C++20)                                     |
|                                                                                        |
|  +--------------------+  +--------------------+  +------------------+  +------------+  |
|  |    dos::Machine    |  |     dos::Ega       |  |   dos::Speaker   |  | net::Modem |  |
|  | 1 MB real-mode mem |  | 320x200 Mode 0Dh   |  | PIT ch2 square-  |  |   Bridge   |  |
|  | INT 08h/09h/33h    |  | 4-plane EGA VRAM   |  | wave audio synth |  | 16550 UART |  |
|  | Fast block dispatch|  | CRTC page flipping |  | 44.1 kHz stream  |  | IRQ 4 / 0Ch|  |
|  +--------------------+  +--------------------+  +------------------+  +------------+  |
|            |                                                                 |         |
|  +--------------------+  +--------------------+                +-------------+-------+ |
|  | recompiled segments|  |    Interpreter     |                | MultiplayerManager  | |
|  | (56 C++ switch-    |  |  fallback for stack|                | Outbound Relay NAT  | |
|  |  dispatched units) |  |  thunks & overlays |                | Direct LAN / Loop   | |
|  +--------------------+  +--------------------+                +---------------------+ |
+----------------------------------------------------------------------------------------+
                                            ^
                                            | static translation
+----------------------------------------------------------------------------------------+
|                                tools/chqre (Python 3.12)                               |
|          8086 decoder  *  CFG discovery  *  EGRAPHIC.EXE overlay jump binding          |
+----------------------------------------------------------------------------------------+
                                            ^
                                            | binary analysis
                              Original CHQ.EXE + EGRAPHIC.EXE
```

### Static Recompiler (`tools/chqre`)
The recompiler parses the 16-bit MZ executable (`CHQ.EXE`) and the EGA graphics overlay (`EGRAPHIC.EXE`), resolves relocations, binds the 58-entry `OVR:` graphics jump table at `1786:0A5C`, and performs iterative recursive-descent control-flow discovery across **67,009 instructions in 17,175 basic blocks** with zero decode errors:
- **Overlay binding**: Relocates `EGRAPHIC.EXE` to its runtime segment (`0x29B0`) and links all active graphics driver entry points into the static call graph.
- **Native Save/Resume Hooking**: Automatically hooks DOS file prompt routines (`0x102ef` for "Resume a saved war" and `0x1049e` for in-game SAVE) into native Cocoa file dialogues and save validation.
- **No-return runtime procedures**: Stops control-flow traversal after Microsoft C 5.1 `_exit`/`__exit`/`_amsg_exit` routines so trailing data tables are never misdecoded as code.
- **Segment translation**: Emits 56 C++ switch-dispatched translation units (`build/generated/seg_XXXX.cpp`) plus `program.cpp`.

### Emulation Layer (`engine/src/dos`)
- **`chq::dos::Machine`**: Manages the 1 MB real-mode memory map, CPU register state, flags, segment registers, hardware interrupts (`INT 08h` timer, `INT 09h` keyboard, `INT 0Ch` serial IRQ 4), BIOS/DOS interrupts (`INT 10h`, `INT 16h`, `INT 1Ah`, `INT 21h` including overlay loading and directory enumeration), and Microsoft Mouse (`INT 33h`) callbacks.
- **Native Save Dialogs**: Transparently hooks the DOS save routines to native macOS save panels with `.SG` header validation (`validate_save_file`).
- **Manual Copy-Protection Convenience Hook**: Automatically populates the unpacked manual verification word when the prompt initializes; configurable via a single on/off toggle in macOS Settings.
- **EGA Display (`dos::Ega`)**: Emulates 320×200 16-color Mode `0Dh` planar EGA memory (`A000:0000`), write modes 0–2, ALU latches, CRTC cursor/start-address registers (`3D4h`/`3D5h`) for multi-page flipping, and 4:3 CRT presentation without cursor bleed.
- **Audio & PIT Timing (`dos::Speaker`)**: Synthesizes 44.1 kHz square-wave audio from 8253 PIT channel 2 and port `61h` speaker modulation.

---

## Modern Internet Multiplayer Architecture

Command HQ was designed in 1990 for 1200 baud direct serial connections and dial-up telephone modems via `SERIAL.OVL`. To adapt the original game for modern internet play across residential connections (traversing CGNAT, carrier firewalls, and home routers without manual port forwarding), we designed a clean-room networking abstraction layer:

```
+--------------------+                         +---------------------+                         +--------------------+
|   Host Player      |                         | Intermediary Broker |                         |   Guest Player     |
| (Residential NAT)  |                         |    (RelayServer)    |                         | (Residential NAT)  |
+--------------------+                         +---------------------+                         +--------------------+
          |                                               |                                               |
          |  1. Connect outbound (TCP)                     |                                               |
          |---------------------------------------------->|                                               |
          |  2. "CREATE WAR-8KT2\n"                       |                                               |
          |---------------------------------------------->|                                               |
          |  3. "CREATED WAR-8KT2\n"                      |                                               |
          |<----------------------------------------------|                                               |
          |                                               |                                               |
          |            (Host shares room code "WAR-8KT2" via message, voice, or chat)                      |
          |                                               |                                               |
          |                                               |  4. Connect outbound (TCP)                    |
          |                                               |<----------------------------------------------|
          |                                               |  5. "JOIN WAR-8KT2\n"                         |
          |                                               |<----------------------------------------------|
          |                                               |  6. "JOINED WAR-8KT2\n"                       |
          |                                               |---------------------------------------------->|
          |  7. "PEER_JOINED\n"                           |                                               |
          |<----------------------------------------------|                                               |
          |                                               |                                               |
          |===============================================+==============================================|
          |                     Framed Binary Packet Relay ([0x55][type][len][payload])                   |
          |---------------------------------------------->|---------------------------------------------->|
          |<----------------------------------------------|<----------------------------------------------|
```

### Components
1. **`IMultiplayerTransport`**: Abstract interface providing connection lifecycle states (`Disconnected`, `Hosting`, `Connecting`, `Connected`, `Faulted`), byte-level send/receive, and latency diagnostics.
2. **`RelayTransport` & `RelayServer`**:
   - **NAT Traversal**: Both players establish outbound TCP connections to an intermediary relay broker. Neither player requires a public IP address or port forwarding.
   - **Room Codes**: Matches are identified by short 4-character room codes (e.g. `WAR-8KT2`).
   - **Binary Framing & Heartbeats**: A 4-byte header (`[0x55][type][len_hi][len_lo]`) frames packets to prevent TCP coalescing and interleaves ping/pong keepalives for live round-trip latency tracking.
3. **`ModemBridge`**:
   - Virtual 16550 UART controller emulating ports `0x3F8`–`0x3FF` (IER, IIR, FCR, LCR, MCR, LSR, MSR, baud divisor).
   - Hayes AT command processor: intercepts `ATZ`, `ATE0`/`ATE1`, `ATH0`/`ATH1`, `ATA` (Answer), and `ATD...` (Dial).
   - Translates connect events into carrier detect signals and raises IRQ 4 (`INT 0Ch`) when data arrives, enabling the original game's `SERIAL.OVL` driver to run unmodified.
4. **`DirectSocketTransport`**: Direct TCP connection mode for local LAN, VPN, or Tailscale play.
5. **`LoopbackTransport`**: In-memory, thread-safe transport pair for automated testing.
6. **`MultiplayerManager`**: Singleton coordinator managing active transports, embedded relay server support for zero-config self-hosting, and live status metrics.

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

## Playing Multiplayer Matches

### Internet Match (NAT Traversal via Room Code)
1. **Host Player**:
   - In the macOS menu bar, select **Multiplayer → Host Internet Match…**.
   - Confirm or enter the relay server address (defaults to embedded local broker or `CHQ_RELAY_SERVER`).
   - Click **Host Match**. A Room Code is generated (e.g. `WAR-8KT2`) and can be copied to your clipboard.
   - In Command HQ, select **Play Game → 2-Player → Modem → Answer**.
2. **Guest Player**:
   - In the macOS menu bar, select **Multiplayer → Join Internet Match…**.
   - Enter the Host's Room Code and click **Join Match**.
   - In Command HQ, select **Play Game → 2-Player → Modem → Call (Dial)**.
3. The modems will establish connection (`CONNECT 1200`) and the match begins. The menu item displays live round-trip latency (e.g. `Status: Connected (WAR-8KT2 | 24 ms)`).

### Direct LAN / IP Match
1. **Host**: Select **Multiplayer → Direct LAN / IP Match… → Host (Listen)** on port 7190. In-game, select **Direct** or **Modem → Answer**.
2. **Guest**: Select **Multiplayer → Direct LAN / IP Match… → Join (Connect)**, enter Host IP and port. In-game, select **Direct** or **Modem → Call**.

---

## In-Game Controls & Features

| Key / Mouse | Action |
|---|---|
| **Left-Click** | Select menus, select units, and set movement destinations on the world map |
| **Right-Click** | Inspect city/region production or cancel current unit selection |
| **Return / Space** | Confirm dialogs, pass pre-filled manual verification prompt, advance screens |
| **Escape** | Open in-game options menu |
| **Option-P / Alt-P** | Pause / unpause war |
| **Cmd-S** | Save war (opens native Cocoa Save As dialog if unassigned) |
| **Cmd-Shift-S** | Save war as (choose custom destination) |
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
  ├── CMakeLists.txt     Build configuration for native, Linux, and Emscripten targets
  ├── resources/         macOS Info.plist, entitlements, and AppIcon.icns
  ├── src/
  │   ├── app/           Application shell (main.cpp, headless, lockstep, macOS bridge)
  │   ├── core/          Byte reading, MZ image parsing, save validation, asset paths
  │   ├── dos/           PC machine, 8086 interpreter, EGA Mode 0Dh, INT 33h mouse, PIT speaker
  │   ├── gfx/           320x200 EGA surface and FONTS.CHQ bitmap font renderer
  │   └── net/           Multiplayer transport, RelayServer, RelayTransport, ModemBridge, Manager
  ├── web/               HTML/CSS shell page for WebAssembly build
  └── tests/             C++ unit tests (doctest), multiplayer tests, and Unicorn corpus verifier
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
Runs all 27 Python recompiler tests and all 5 C++ test suites on macOS:
```zsh
./scripts/test.zsh
```

Test suites include:
- `unit`: Core byte reader, MZ loader, save validation, mouse control, host keys, and multiplayer transport layer.
- `instruction_corpus`: Compares instruction decoder and flag calculation against the Unicorn x86 emulator across 7,500+ cases.
- `game_headless`: Multi-thousand-frame gameplay session exercising map movement, orders, and options.
- `game_lockstep`: Runs recompiled code and interpreted code in lockstep block by block, verifying state parity.
- `app_smoke`: Verification of SDL application startup, frame ticking, and exit.

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

---

## Clean-Room & Security Policy

- Original binary files (`*.EXE`, `*.PCX`, `*.WAL`, `*.DAT`, `*.CHQ`, `*.SG`, `*.DRV`, `*.OVL`) are ignored by git and never committed.
- Recompiled C++ translation units (`build/generated/*.cpp`) are generated locally from the user's binary and never committed.
- All test suites use mock byte sequences, synthetic MZ executables, or in-memory loopback transports to enable verification in clean CI environments without external game assets.
