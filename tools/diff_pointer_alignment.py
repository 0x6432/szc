#!/usr/bin/env python3
"""Compare alignment-qualified pointer coercions, identities and joins with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def pointer(alignment, child="u32", const=False):
    return "*" + (f"align({alignment}) " if alignment else "") + ("const " if const else "") + child


def conversions(source, destination):
    yield (f"fn convert(p: {source}) {destination} {{ return p; }}\n"
           "pub fn main() void { _ = convert(undefined); }\n")
    yield (f"pub fn main() void {{ const p: {source} = undefined; "
           f"const q: {destination} = p; _ = q; }}\n")


def cases():
    alignments = (0, 1, 2, 4, 8, 16)
    types = [pointer(a, const=c) for a, c in itertools.product(alignments, (False, True))]
    for source, destination in itertools.product(types, repeat=2):
        yield from conversions(source, destination)
    types = [pointer(a, pointer(b), c)
             for a, b, c in itertools.product((0, 1), (0, 1, 4), (False, True))]
    for source, destination in itertools.product(types, repeat=2):
        yield from conversions(source, destination)
    for left, right, destination in itertools.product(alignments, alignments, (0, 1, 4)):
        yield (f"fn choose(c: bool, a: {pointer(left, const=True)}, "
               f"b: {pointer(right)}) {pointer(destination, const=True)} {{ "
               "const p = if (c) a else b; return p; }\n"
               "pub fn main() void { _ = choose(true, undefined, undefined); }\n")
    functions = [pointer(a, f"fn ({pointer(p)}) {pointer(r)}", True)
                 for a, p, r in itertools.product((0, 1, 16), (1, 4), (1, 4))]
    for source, destination in itertools.product(functions, repeat=2):
        yield from conversions(source, destination)
    for left, right in itertools.product(alignments, repeat=2):
        # Explicit ABI alignment is memory-compatible, but not type-identical, to implicit.
        operator = "!=" if left == right else "=="
        yield (f"pub fn main() void {{ if ({pointer(left)} {operator} {pointer(right)}) "
               "{ const bad: u8 = true; _ = bad; } }\n")
    for alignment, const in itertools.product((0, 1, 16), (False, True)):
        for reverse in (False, True):
            left, right = ("p", "target") if reverse else ("target", "p")
            yield ("fn target() u8 { return 7; }\n"
                   f"fn choose(c: bool, p: {pointer(alignment, 'fn () u8', const)}) u8 {{ "
                   f"const f = if (c) {left} else {right}; return f(); }}\n"
                   "pub fn main() void { _ = choose(true, undefined); }\n")

    # Compile-time bodies must never emit runtime control flow, loads, or casts.
    yield 'pub fn main() void { const P = *align(unreachable) u32; _ = P; }\n'
    yield 'fn f(n: u8) void { const P = *align(n) u8; _ = P; } pub fn main() void { f(4); }\n'
    yield 'fn f(n: u64) void { const P = *align(@intCast(n)) u8; _ = P; } pub fn main() void { f(4); }\n'
    yield 'fn f(n: u64) void { const P = *align(@truncate(n)) u8; _ = P; } pub fn main() void { f(4); }\n'
    yield 'fn f(n: bool) void { const P = *align(@intFromBool(n)) u8; _ = P; } pub fn main() void { f(true); }\n'
    yield 'fn f(n: u29) void { const P = *align(~n) u8; _ = P; } pub fn main() void { f(4); }\n'
    yield 'fn f(n: i29) void { const P = *align(@intCast(-n)) u8; _ = P; } pub fn main() void { f(-4); }\n'
    yield 'fn f(n: bool) void { const P = *align(@intFromBool(!n)) u8; _ = P; } pub fn main() void { f(false); }\n'
    yield 'fn f(n: bool) void { const P = *align(@intFromBool(n and true)) u8; _ = P; } pub fn main() void { f(true); }\n'
    yield 'fn f(p: *u8) void { const P = *align(blk: { p.* = 2; break :blk 4; }) u8; _ = P; } pub fn main() void { var n: u8 = 1; f(&n); }\n'
    yield 'fn f(p: *u8) void { const P = *align(blk: { p.* = undefined; break :blk 4; }) u8; _ = P; } pub fn main() void { var n: u8 = 1; f(&n); }\n'
    yield 'var n: u29 = 4; pub fn main() void { const P = *align(n) u8; _ = P; }\n'
    yield "fn f(n: u64) void { const P = *align(@as(u29, @as(u0, @truncate(n))) + 1) u8; _ = P; } pub fn main() void { f(7); }\n"
    yield "fn f(n: u29) void { const P = *align(if (true) 4 else n) u8; _ = P; } pub fn main() void { f(8); }\n"

    yield 'pub fn main() void { const P = *align(@panic("alignment panic")) u8; _ = P; }\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-pointer-alignment-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                f"diff_pointer_alignment case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                f"diff_pointer_alignment case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"pointer alignment: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
