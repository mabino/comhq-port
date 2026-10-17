# Reverse-engineering findings

Facts established about `CHQ.EXE` and `EGRAPHIC.EXE` (*Command HQ*, 1990, Dan Bunten / Ozark Softscape / MicroProse).
Regenerate the raw data with `scripts/inventory.zsh` and `scripts/analyze.zsh`; it lands in `out/`.

## Binaries

| Property | `CHQ.EXE` | `EGRAPHIC.EXE` |
|---|---|---|
| Format | MS-DOS MZ, 287,620 bytes, not packed | MS-DOS MZ overlay driver, 11,488 bytes |
| sha256 | `e3859af8026be252…` (full value in `out/inventory.json`) | `0317976f57d7d972…` |
| Load image | 265,200 bytes (`0x40BF0`), 5,368 relocations | 10,976 bytes (`0x2AE0`), 25 relocations |
| Entry | `19FE:0010` (`__astart`), stack `2F5A:7742` | `0000:0000` (58-entry `OVR:` jump table) |
| Compiler / Toolchain | Microsoft C 5.1 (Large Model) + Ozark Softscape assembly modules | Custom 8086 EGA Mode `0Dh` graphics driver (`EGADRV00.EXE`, `09/05/90`) |

## Segment map (`CHQ.EXE` + `EGRAPHIC.EXE`)

`CHQ.EXE` is loaded at `0x1000:0x0000` (relocated segments `0x1000..0x3F5A`), with `DGROUP` at `0x3F5A` (`0x2F5A` relative to `0x1000`). At startup, `1786:0497` loads `EGRAPHIC.EXE` via `INT 21h AH=4Bh AL=03h` (Load Overlay) into segment `0x29B0` (`0x19B0` relative to `0x1000`) and patches 58 jump vectors in `1786:0A5C..0BC9`.

| Seg (rel) | Bytes | Role |
|---|---|---|
| `0000` | 20,768 | Scenario setup, map initialization, unit placement (`NO MAP FILE`) |
| `0512` | 15,792 | Strategic AI, combat evaluation, and pathfinding |
| `08ED` | 15,888 | World map rendering, region & city status, unit movement |
| `0CCE` | 16,944 | Replay / War Film recorder and playback (`C_HQ.REP`, `SAVE ERROR`) |
| `10F1` | 12,272 | Resource / text table decompression (`TEXT.DAT`, manual copy-protection word unpacker at `10F1:2D50`) |
| `13F0` | 14,688 | UI dialogs, text input (`13F0:0012`), save/load menu (`CHQ-00.SG`) |
| `1786` | 10,112 | Hardware & driver layer: `INT 08h`/`09h` ISRs, `INT 33h` mouse wrapper, `EGRAPHIC.EXE` (`0A5C`) & `SERIAL.OVL` (`0CA4`) jump tables |
| `19FE` | 10,800 | Microsoft C 5.1 startup (`19FE:0010`), DOS memory/file runtime, `int86x` (`19FE:1656`), `allmul`/`uldiv` |
| `1CA1` | 64 | Resident sound driver stub (`IBM.DRV` / `ADLIB.DRV` / `ROLAND.DRV` / `TANDY.DRV`) |
| `1CA5` | 816 | PCX RLE scanline decoder & planar blit helpers |
| `1CD8` | 5,904 | Modem / serial / two-player synchronization (`SERIAL.OVL` interface) |
| `1E49` | 7,008 | Economic production, oil/resource allocation, and diplomacy |
| `1FFF` | 27,200 | Main game loop, turn/real-time clock, strategic orders, unit combat |
| `26A3` | 12,192 | Title sequence (`CHQTITLE.PCX`), credits (`CREDITS.PCX`), manual verification (`1AA3:118C`), main menu (`MAINMENU.PCX`) |
| `299D` | 23,504 | Data group (`DGROUP`, relocated to `0x3F5A`): initialized globals, strings, stack, and near heap |
| `19B0` | 10,976 | `EGRAPHIC.EXE` (relocated to `0x29B0`): 320×200 16-color Mode `0Dh` planar EGA primitives, `FONTS.CHQ` proportional font blitter |

## Static Recompiler Architecture (`tools/chqre`)

1. **Combined `CHQ.EXE` + `EGRAPHIC.EXE` Static Image (`chqre.cfg`)**:
   - `chqre` loads `CHQ.EXE` at `0x1000` (`265,200` bytes) and embeds the relocated `EGRAPHIC.EXE` image (`10,976` bytes) at `0x29B0` (`offset 0x19B00` in the combined `276,176`-byte code image).
   - It pre-populates the 58 `OVR:` jump table entries at `1786:0A5C..0BC9` with `JMP FAR 29B0:xxxx` so recursive-descent CFG discovery traces seamlessly from game code through `1786` into `EGRAPHIC.EXE`.
   - Across `CHQ.EXE` and `EGRAPHIC.EXE`, `chqre recomp` discovers **65,209 instructions in 16,692 basic blocks** across 36 code segments with **0 decode errors**.

2. **Hardware Interrupts & Input (`1786:005A`, `1786:0079`, `1786:00AD`, `1786:032D`)**:
   - `1786:005A` hooks `INT 08h` (`1786:0079`) and `INT 09h` (`1786:00AD`), and initializes the mouse driver via `INT 33h` (`AX=0000h`, `0007h`, `0008h`, `000Ch` callback at `1786:0421`).
   - `1786:00AD` reads XT make/break scancodes from port `0x60` and pushes translated key words into a 20-entry ring buffer at `DS:0x277F` (`head = DS:0x277B`, `tail = DS:0x277D`).
   - All keyboard polling goes through `1786:032D` (`_get_key`) and `1786:0351` (`_peek_key`). `Machine::rd8l` detects empty ring-buffer reads at `0x3F5A:0x277D` and yields (`waiting_key_ = true`).

3. **Manual Verification Prompt (`1AA3:118C`, `10F1:2D50`, `13F0:0012`)**:
   - At `1AA3:11AF`, `CALL FAR 20F1:2D50` unpacks the randomly selected manual verification word (up to 8 ASCII characters + NUL) from `TEXT.DAT` into `DS:0xE73A` (`0x3F5A:0xE73A`), with page number at `DS:0xC5B8`, paragraph at `DS:0xF340`, and word number at `DS:0xDD6E`.
   - At `1AA3:115F..1165`, single-player mode (`DS:[0xC5B5] == 0`) copies only the first character of `DS:0xE73A` into the input buffer `DS:0xCA86` and writes `0` to `DS:0xCA87` before calling `13F0:0012`.
   - `13F0:0012` prints the initial string in `DS:0xCA86` to the prompt box and waits for `Enter` (`0x0D`). When `prefill_manual_word` is enabled (default `true`), `Machine::wr8l` intercepts the write to `0x3F5A:0xCA87` from `CS=0x2AA3` and copies the remaining characters of `DS:0xE73A` into `DS:0xCA86..0xCA8E`, displaying the complete manual word on screen so pressing `Enter` immediately passes the check.

4. **EGA Mode `0Dh` & Color Display Detection (`29B0:0123..0158`)**:
   - `EGRAPHIC.EXE` verifies a color EGA adapter at `29B0:0134` by writing `0x2D` to CRTC port `0x3D5` (index `0x0F`, cursor location low) and reading `0x3D5` back at `29B0:013E`.
   - `EGRAPHIC.EXE` uses 320×200 16-color planar EGA (`Mode 0Dh`, 40 bytes per scanline) with multiple video pages in `A000:0000..A000:7FFF` selected via CRTC start-address registers `0x0C`/`0x0D` (`0x0000`, `0x2000`, `0x4000`, `0x6000`).

5. **Saved Games (`CHQ-00.SG` .. `CHQ-99.SG`)**:
   - Save files begin with a 16-bit header word `0x13EB` (`5,099` bytes), a 20-byte space/NUL-padded ASCII description at offset `8..27`, and `5,099` bytes of serialized `DGROUP` game state (followed by optional War Film history packets).
