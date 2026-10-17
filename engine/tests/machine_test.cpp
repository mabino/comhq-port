// The emulated PC with tiny hand-assembled programs (run by the interpreter;
// no recompiled code and no game data needed).
#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dos/machine.h"

namespace fs = std::filesystem;
using chq::dos::Machine;

namespace {

constexpr std::uint16_t kLoad = 0x1000;

struct Prog {
    std::vector<std::uint8_t> image;
    chq::dos::Program program;
    explicit Prog(std::vector<std::uint8_t> code) : image(std::move(code)) {
        image.resize(image.size() + 0x200);
        const auto sp = static_cast<std::uint16_t>(image.size());
        program = chq::dos::Program{image, kLoad, kLoad, 0, kLoad, sp, 0, {}, {}, {}, {}};
    }
};

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("chq-machine-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

Machine::State run_to_end(Machine& m, int slices = 50) {
    for (int i = 0; i < slices && m.state() == Machine::State::Running; ++i) m.run(100000, i * 16.0);
    return m.state();
}

std::vector<std::uint8_t> bytes(std::initializer_list<int> list) {
    std::vector<std::uint8_t> v;
    for (int b : list) v.push_back(static_cast<std::uint8_t>(b));
    return v;
}

}  // namespace

TEST_CASE("DOS console output reaches text memory, exit code is returned") {
    Prog p(bytes({0x8C, 0xC8, 0x8E, 0xD8, 0xBA, 0x16, 0x00, 0xB4, 0x40, 0xBB, 0x01, 0x00, 0xB9, 0x02, 0x00, 0xCD,
                  0x21, 0xB8, 0x2A, 0x4C, 0xCD, 0x21, 'H', 'I'}));
    Machine m(p.program, {});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 0x2A);
    CHECK(m.mem()[0xB8000] == 'H');
    CHECK(m.mem()[0xB8002] == 'I');
}

TEST_CASE("files are created in the save folder and read back") {
    TempDir saves, data;
    std::ofstream(data.path / "ORIG.DAT") << "abc";
    std::vector<std::uint8_t> code = bytes({
        0x8C, 0xC8, 0x8E, 0xD8,              // mov ax,cs ; mov ds,ax
        0xB4, 0x3C, 0x31, 0xC9,              // mov ah,3Ch ; xor cx,cx
        0xBA, 0x60, 0x00, 0xCD, 0x21,        // mov dx,0060h ; int 21h   (create "TEST.DAT")
        0x89, 0xC3,                          // mov bx,ax
        0xB4, 0x40, 0xB9, 0x03, 0x00,        // mov ah,40h ; mov cx,3
        0xBA, 0x70, 0x00, 0xCD, 0x21,        // mov dx,0070h ; int 21h   (write "xyz")
        0xB4, 0x3E, 0xCD, 0x21,              // mov ah,3Eh ; int 21h     (close)
        0xB8, 0x00, 0x3D,                    // mov ax,3D00h
        0xBA, 0x80, 0x00, 0xCD, 0x21,        // mov dx,0080h ; int 21h   (open "orig.dat")
        0x89, 0xC3,                          // mov bx,ax
        0xB4, 0x3F, 0xB9, 0x03, 0x00,        // mov ah,3Fh ; mov cx,3
        0xBA, 0x00, 0x01, 0xCD, 0x21,        // mov dx,0100h ; int 21h   (read)
        0xA0, 0x00, 0x01,                    // mov al,[0100h]
        0x02, 0x06, 0x01, 0x01,              // add al,[0101h]
        0x02, 0x06, 0x02, 0x01,              // add al,[0102h]
        0xB4, 0x4C, 0xCD, 0x21,              // mov ah,4Ch ; int 21h
    });
    code.resize(0x60, 0x90);
    for (char ch : std::string("TEST.DAT")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x70, 0);
    for (char ch : std::string("xyz")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x80, 0);
    for (char ch : std::string("orig.dat")) code.push_back(static_cast<std::uint8_t>(ch));
    code.resize(0x90, 0);
    Prog p(code);
    Machine m(p.program, {data.path, saves.path});
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == (('a' + 'b' + 'c') & 0xFF));
    std::ifstream written(saves.path / "TEST.DAT");
    std::string text;
    written >> text;
    CHECK(text == "xyz");
}

TEST_CASE("INT 16h blocks until a key arrives") {
    Prog p(bytes({0x30, 0xE4, 0xCD, 0x16, 0xB4, 0x4C, 0xCD, 0x21}));
    Machine m(p.program, {});
    CHECK(m.run(100000, 0) == Machine::State::Running);
    CHECK(m.waiting_for_key());
    m.key_down(0x1E, 'a');
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 'a');
}

TEST_CASE("INT 33h mouse driver reports position and invokes registered callback") {
    // Hook INT 33h AX=0Ch callback at CS:0040h, which stores CX+DX into [0100h] and retf.
    std::vector<std::uint8_t> code = bytes({
        0x8C, 0xC8, 0x8E, 0xD8, 0x8E, 0xC0,  // mov ax,cs ; mov ds,ax ; mov es,ax
        0xB8, 0x00, 0x00, 0xCD, 0x33,        // mov ax,0 ; int 33h (init mouse)
        0xB8, 0x0C, 0x00, 0xB9, 0x1F, 0x00,  // mov ax,0Ch ; mov cx,1Fh
        0xBA, 0x40, 0x00, 0xCD, 0x33,        // mov dx,0040h ; int 33h (register callback)
        0xB4, 0x00, 0xCD, 0x16,              // wait for key so host can move mouse
        0xA0, 0x00, 0x01,                    // mov al,[0100h]
        0xB4, 0x4C, 0xCD, 0x21,              // mov ah,4Ch ; int 21h
    });
    code.resize(0x40, 0x90);
    // Callback at 0040h: add cx,dx ; add cx,bx ; mov cs:[0100h],cl ; retf
    for (int b : {0x01, 0xD1, 0x01, 0xD9, 0x2E, 0x88, 0x0E, 0x00, 0x01, 0xCB})
        code.push_back(static_cast<std::uint8_t>(b));
    code.resize(0x110, 0);
    Prog p(code);
    Machine m(p.program, {});
    CHECK(m.run(1000, 0) == Machine::State::Running);
    m.set_mouse(120, 45, 1);  // invokes callback with CX=120, DX=45, BX=1 -> 166
    m.key_down(0x1C, 0x0D);
    REQUIRE(run_to_end(m) == Machine::State::Exited);
    CHECK(m.exit_code() == 166);
}

TEST_CASE("executing outside conventional memory is a fault, not a crash") {
    Prog p(bytes({0xEA, 0x00, 0x00, 0x00, 0xB0}));  // jmp far B000:0000
    Machine m(p.program, {});
    CHECK(run_to_end(m) == Machine::State::Faulted);
    CHECK(m.fault().find("left conventional memory at B000:0000") != std::string::npos);
}

TEST_CASE("BIOS clock follows host time") {
    Prog p(bytes({0xEB, 0xFE}));  // jmp $
    Machine m(p.program, {});
    m.set_clock_override(0.0);
    m.run(1000, 0.0);
    const std::uint32_t t0 = m.mem()[0x46C] | m.mem()[0x46D] << 8;
    m.set_clock_override(1000.0);
    m.run(1000, 1000.0);
    const std::uint32_t t1 = m.mem()[0x46C] | m.mem()[0x46D] << 8;
    CHECK(t1 - t0 == 18);  // 18.2 ticks per second
}

TEST_CASE("PC speaker: PIT channel 2 square wave while port 61h enables it") {
    Prog p(bytes({0xB0, 0xB6, 0xE6, 0x43, 0xB0, 0xA9, 0xE6, 0x42, 0xB0, 0x04, 0xE6, 0x42, 0xE4, 0x61, 0x0C, 0x03,
                  0xE6, 0x61, 0xEB, 0xFE}));
    Machine m(p.program, {});
    m.run(1000, 0.0);
    CHECK(m.speaker().on());
    CHECK(m.speaker().frequency() == doctest::Approx(1000.15).epsilon(0.001));
    std::vector<float> out;
    m.speaker().render(out, 8000, 10.0);
    CHECK(out.size() == 80);
    int sign_changes = 0;
    for (std::size_t i = 1; i < out.size(); ++i) sign_changes += (out[i] > 0) != (out[i - 1] > 0);
    CHECK(sign_changes >= 18);
}

TEST_CASE("PIT channel 0: custom frequency delivery and BIOS INT 08h chaining") {
    // Hook INT 08h to CS:0040h.
    // At CS:0040h:
    //   inc word ptr cs:[0100h]   (count ticks)
    //   dec byte ptr cs:[0102h]
    //   jnz not_13
    //   mov byte ptr cs:[0102h], 13
    //   jmp far cs:[0104h]        (chain to BIOS stub at F000:0120)
    // not_13:
    //   push ax ; mov al, 20h ; out 20h, al ; pop ax ; iret
    // Main code at CS:0000h:
    //   Save old INT 08h to cs:[0104h]
    //   Set INT 08h vector to CS:0040h
    //   Reprogram PIT0 divisor to 5041 (13B1h)
    //   Loop (jmp $)
    std::vector<std::uint8_t> code(0x200, 0);
    // Main:
    // xor ax, ax ; mov es, ax
    // mov bx, es:[0020h] ; mov cs:[0104h], bx
    // mov bx, es:[0022h] ; mov cs:[0106h], bx
    // mov es:[0020h], 0040h
    // mov es:[0022h], cs
    // mov al, 36h ; out 43h, al
    // mov al, b1h ; out 40h, al
    // mov al, 13h ; out 40h, al
    // mov byte ptr cs:[0102h], 13
    // loop: jmp loop
    const std::vector<std::uint8_t> main_bytes = {
        0x31, 0xC0, 0x8E, 0xC0,                          // xor ax, ax ; mov es, ax
        0x26, 0x8B, 0x1E, 0x20, 0x00,                    // mov bx, es:[0020h]
        0x2E, 0x89, 0x1E, 0x04, 0x01,                    // mov cs:[0104h], bx
        0x26, 0x8B, 0x1E, 0x22, 0x00,                    // mov bx, es:[0022h]
        0x2E, 0x89, 0x1E, 0x06, 0x01,                    // mov cs:[0106h], bx
        0x26, 0xC7, 0x06, 0x20, 0x00, 0x40, 0x00,        // mov word ptr es:[0020h], 0040h
        0x8C, 0xC8, 0x26, 0x89, 0x06, 0x22, 0x00,        // mov ax, cs ; mov es:[0022h], ax
        0xB0, 0x36, 0xE6, 0x43,                          // mov al, 36h ; out 43h, al
        0xB0, 0xB1, 0xE6, 0x40,                          // mov al, B1h ; out 40h, al
        0xB0, 0x13, 0xE6, 0x40,                          // mov al, 13h ; out 40h, al
        0x2E, 0xC6, 0x06, 0x02, 0x01, 0x0D,              // mov byte ptr cs:[0102h], 0Dh
        0xFB,                                            // sti
        0xEB, 0xFE,                                      // jmp $
    };
    std::copy(main_bytes.begin(), main_bytes.end(), code.begin());

    // Handler at 0x40:
    // 2E FF 06 00 01       inc word ptr cs:[0100h]
    // 2E FE 0E 02 01       dec byte ptr cs:[0102h]
    // 75 0C                jnz not_13
    // 2E C6 06 02 01 0D    mov byte ptr cs:[0102h], 0Dh
    // 2E FF 2E 04 01       jmp far ptr cs:[0104h]
    // not_13:
    // 50                   push ax
    // B0 20                mov al, 20h
    // E6 20                out 20h, al
    // 58                   pop ax
    // CF                   iret
    const std::vector<std::uint8_t> handler_bytes = {
        0x2E, 0xFF, 0x06, 0x00, 0x01,
        0x2E, 0xFE, 0x0E, 0x02, 0x01,
        0x75, 0x0B,
        0x2E, 0xC6, 0x06, 0x02, 0x01, 0x0D,
        0x2E, 0xFF, 0x2E, 0x04, 0x01,
        0x50, 0xB0, 0x20, 0xE6, 0x20, 0x58, 0xCF
    };
    std::copy(handler_bytes.begin(), handler_bytes.end(), code.begin() + 0x40);

    Prog p(code);
    Machine m(p.program, {});
    m.set_clock_override(0.0);
    m.run(10000, 0.0);

    // Run for 100 milliseconds at 236.7 Hz -> should receive ~23-24 ticks.
    // Tick 13 chains to BIOS stub; subsequent ticks must continue to arrive.
    for (int frame = 1; frame <= 6; ++frame) {
        const double ms = frame * (1000.0 / 60.0);
        m.set_clock_override(ms);
        m.run(10000, ms);
    }
    const std::uint16_t tick_count = m.rd16(p.program.load_segment, 0x0100);
    CHECK(tick_count >= 22);
    CHECK(tick_count <= 26);
}

