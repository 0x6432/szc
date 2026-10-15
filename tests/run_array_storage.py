#!/usr/bin/env python3
"""Check immutable array bytes, alignment and relocations in real PIE/RELRO storage."""
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run as run_bounded


def run(command, env=None, timeout=30):
    if env is not None:
        command = ["env", "SZC_CC=" + env["SZC_CC"], *command]
    result = run_bounded(command, timeout=timeout)
    assert result.returncode == 0, f"{command!r}: exit {result.returncode}\n" + result.stderr.decode(errors="replace")
    return result.stdout


def sections(executable):
    text = run(["readelf", "-SW", str(executable)]).decode()
    return {int(index): (name, int(address, 16), int(offset, 16), int(size, 16), flags)
            for index, name, address, offset, size, flags in re.findall(
                r"\[\s*(\d+)\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+(\S+)", text)}


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc
    cases = [
        ("signed", "[3]i7", "[_]i7{-63,3,7}", 1, b"\xc1\x03\x07", None, ""),
        ("wide", "[2]u33", "[_]u33{4294967297,8589934591}", 8,
         struct.pack("<QQ", 4294967297, 8589934591), None, ""),
        ("nested", "[2][3]u16", ".{ .{258,3,4}, .{5,6,7} }", 2,
         struct.pack("<6H", 258,3,4,5,6,7), None, ""),
        ("pointers", "[2]*const u8", ".{&x,&y}", 256, None, (b"\x1f", b"\x2f"),
         "const x: u8 = 31; const y: u8 = 47;"),
        ("bytes", "[2]*const [2]u8", '.{"hi","by"}', 64, None, (b"hi\0", b"by\0"), ""),
        ("zero", "[3]u0", "undefined", 1, b"\0", None, ""),
    ]
    passed = 0
    with tempfile.TemporaryDirectory(prefix="szc-array-storage-") as directory:
        root = Path(directory)
        wrapper = root / "cc-pie"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc) + ' -fPIE -pie -Wl,-z,relro,-z,now "$@"\n')
        wrapper.chmod(0o755)
        env = dict(os.environ, SZC_CC=str(wrapper))
        for name, ty, initializer, alignment, expected, targets, prefix in cases:
            source = root / f"{name}.zig"
            source.write_text(prefix + f"\nconst a: {ty} align({alignment}) = {initializer};\n"
                              f"fn length(p: *const {ty}) usize {{ return p.len; }}\n"
                              "pub fn main() void { _ = length(&a); }\n")
            executable = root / name
            run([compiler, "build-exe", str(source), "-o", str(executable)], env)
            assert run([str(executable)]) == b""
            assert "DYN" in run(["readelf", "-h", str(executable)]).decode()
            assert b"TEXTREL" not in run(["readelf", "-d", str(executable)])
            tir = run([compiler, "tir", str(source)]).decode()
            objects = re.findall(r" = const ref\(object\.(\d+)\)", tir)
            assert len(set(objects)) == 1, tir
            symbol = f"szc.const.{objects[0]}"
            symbols = run(["readelf", "-sW", str(executable)]).decode()
            matches = [line.split() for line in symbols.splitlines()
                       if line.split() and line.split()[-1] == symbol]
            assert len(matches) == 1, symbol
            fields = matches[0]
            assert fields[4] == "LOCAL"
            address = int(fields[1], 16)
            section_map = sections(executable)
            section = section_map[int(fields[6])]
            assert address % alignment == 0, (name, alignment, address)
            data = executable.read_bytes()

            def at_address(value, count):
                for _, base, offset, size, flags in section_map.values():
                    if base <= value and value + count <= base + size and "A" in flags:
                        return data[offset + value - base:offset + value - base + count]
                raise AssertionError(f"no file-backed section at {value:x}")

            if targets is None:
                assert section[0] == ".rodata" and "W" not in section[4]
                assert at_address(address, len(expected)) == expected, name
            else:
                assert section[0] == ".data.rel.ro"
                pointers = struct.unpack("<QQ", at_address(address, 16))
                for pointer, expected_target in zip(pointers, targets):
                    assert at_address(pointer, len(expected_target)) == expected_target, name
                protected = False
                for line in run(["readelf", "-lW", str(executable)]).decode().splitlines():
                    fields = line.split()
                    if fields and fields[0] == "GNU_RELRO":
                        base, length = int(fields[2], 16), int(fields[5], 16)
                        protected |= base <= address and address + 16 <= base + length
                assert protected, name
            zig = os.environ.get("ZIG")
            if zig:
                reference = root / "reference"
                run([zig, "build-exe", str(source), "-j2", f"-femit-bin={reference}"], timeout=120)
                assert run([str(reference)]) == b""
            passed += 1
        helper = root / "foreign.c"
        helper.write_text("#include <stdint.h>\n"
                          "static const uint8_t si7[] = {0xc1, 0xfe, 0x07};\n"
                          "static const uint8_t ui7[] = {0x00, 0x7f, 0x01};\n"
                          "static const uint64_t si33[] = {0xffffffff00000001ULL, 7, 0xffffffffffffffffULL};\n"
                          "static const uint64_t ui33[] = {5, 0x1ffffffffULL, 1};\n"
                          "const void *signed7(void) { return si7; }\n"
                          "const void *unsigned7(void) { return ui7; }\n"
                          "const void *signed33(void) { return si33; }\n"
                          "const void *unsigned33(void) { return ui33; }\n")
        source = root / "foreign.zig"
        source.write_text("extern fn signed7() *const [3]i7;\n"
                          "extern fn unsigned7() *const [3]u7;\n"
                          "extern fn signed33() *const [3]i33;\n"
                          "extern fn unsigned33() *const [3]u33;\n"
                          "fn read_i7(p: *const [3]i7, n: usize) i7 { return p[n]; }\n"
                          "fn read_u7(p: *const [3]u7, n: usize) u7 { return p[n]; }\n"
                          "fn read_i33(p: *const [3]i33, n: usize) i33 { return p[n]; }\n"
                          "fn read_u33(p: *const [3]u33, n: usize) u33 { return p[n]; }\n"
                          "pub fn main() u8 {\n"
                          "if (read_i7(signed7(), 0) != -63 or read_i7(signed7(), 1) != -2 or read_i7(signed7(), 2) != 7) return 1;\n"
                          "if (read_u7(unsigned7(), 0) != 0 or read_u7(unsigned7(), 1) != 127) return 2;\n"
                          "if (read_i33(signed33(), 0) != -4294967295 or read_i33(signed33(), 1) != 7 or read_i33(signed33(), 2) != -1) return 3;\n"
                          "if (read_u33(unsigned33(), 0) != 5 or read_u33(unsigned33(), 1) != 8589934591) return 4;\n"
                          "return 0; }\n")
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc) + ' -fPIE -pie -Wl,-z,relro,-z,now "$@" '
                           + shlex.quote(str(helper)) + "\n")
        executable = root / "foreign"
        run([compiler, "build-exe", str(source), "-o", str(executable)], env)
        assert run([str(executable)]) == b""
        zig = os.environ.get("ZIG")
        if zig:
            reference = root / "foreign-reference"
            run([zig, "build-exe", str(source), str(helper), "-lc", "-j2", f"-femit-bin={reference}"], timeout=120)
            assert run([str(reference)]) == b""
        passed += 1
    print(f"array storage: {passed} passed, 0 failed")


if __name__ == "__main__":
    main()
