#!/usr/bin/env python3
"""Check mutable array alignment, foreign bytes and overlapping copies through C."""
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run


def checked(command, timeout=30):
    result = run(command, timeout)
    assert result.returncode == 0, f"{command!r}: {result.returncode}\n{result.stderr.decode(errors='replace')}"
    assert not result.stdout and not result.stderr, (command, result.stdout, result.stderr)


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get("SZC_CC", "cc"))
    assert cc
    with tempfile.TemporaryDirectory(prefix="szc-mutable-array-") as directory:
        root = Path(directory)
        helper = root / "helper.c"
        helper.write_text('''#include <stdbool.h>
#include <stdint.h>
#include <string.h>
extern void copy_array(uint8_t *, const uint8_t *);
extern bool from_unaligned(const uint16_t *);
bool verify(const uint16_t *p, const uint16_t *old) {
    return (uintptr_t)p % 256 == 0 && p[0] == 9 && p[1] == 2 && p[2] == 3
        && old[0] == 7 && old[1] == 2 && old[2] == 3;
}
bool check_snapshot(const uint16_t *p) {
    return (uintptr_t)p % 128 == 0 && p[0] == 258 && p[1] == 772;
}
bool check_copies(void) {
    uint8_t a[6] = {1,2,3,4,5,6};
    copy_array(a + 1, a);
    const uint8_t first[6] = {1,1,2,3,4,6};
    if (memcmp(a, first, 6) != 0) return false;
    copy_array(a, a + 1);
    const uint8_t second[6] = {1,2,3,4,4,6};
    if (memcmp(a, second, 6) != 0) return false;
    _Alignas(8) uint8_t bytes[8] = {0,2,1,4,3,0,0,0};
    return from_unaligned((const uint16_t *)(bytes + 1));
}
''')
        wrapper = root / "cc-helper"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(cc)
                           + ' -std=c11 -Wall -Wextra -Werror "$@" ' + shlex.quote(str(helper)) + "\n")
        wrapper.chmod(0o755)
        source = root / "arrays.zig"
        source.write_text('''extern fn verify(p: *align(256) const [3]u16, old: *const [3]u16) bool;
extern fn check_copies() bool;
extern fn check_snapshot(p: *align(128) const [2]u16) bool;
export fn copy_array(dst: *[4]u8, src: *const [4]u8) void { dst.* = src.*; }
export fn from_unaligned(p: *align(1) const [2]u16) bool {
    const snapshot align(128) = p.*;
    return check_snapshot(&snapshot);
}
fn exercise(n: u16) bool {
    var a: [3]u16 align(256) = .{n, 2, 3};
    const old = a;
    a[0] = 9;
    return verify(&a, &old) and check_copies();
}
pub fn main() u8 { if (!exercise(7)) return 1; return 0; }
''')
        executable = root / "arrays"
        checked(["env", "SZC_CC=" + str(wrapper), compiler, "build-exe", str(source), "-o", str(executable)])
        checked([str(executable)])
        zig = os.environ.get("ZIG")
        if zig:
            obj = root / "helper.o"
            checked([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-c", str(helper), "-o", str(obj)])
            reference = root / "reference"
            checked([zig, "build-exe", str(source), str(obj), "-lc", "-femit-bin=" + str(reference)], 120)
            checked([str(reference)])
    print("mutable array storage: alignment, unaligned snapshots, overlapping copies passed")


if __name__ == "__main__":
    main()
