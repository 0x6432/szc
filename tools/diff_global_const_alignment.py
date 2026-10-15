#!/usr/bin/env python3
"""Compare aligned global constants, copied bindings and comptime-only restrictions with Zig."""
import argparse
import itertools
from pathlib import Path
import tempfile

from compiler_process import run_compiler


def pointer(ty, alignment, const):
    return "*" + (f"align({alignment}) " if alignment else "") + ("const " if const else "") + ty


def cases():
    for ty, storage, destination, const, inferred in itertools.product(
            ("u8", "u32", "u64"), (0, 1, 4, 16, 64), (0, 1, 4, 16, 64), (False, True), (False, True)):
        annotation = "" if inferred else f": {ty}"
        alignment = f" align({storage})" if storage else ""
        yield (f"const value{annotation}{alignment} = @as({ty}, 7);\n"
               f"pub fn main() void {{ const p: {pointer(ty, destination, const)} = &value; _ = p.*; }}\n")

    for storage, destination, typed, local in itertools.product((1, 64), (1, 4, 64), (False, True), (False, True)):
        annotation = ": u32" if typed else ""
        alias = f"const alias{annotation} = value; "
        yield (f"const value: u32 align({storage}) = 7;\n"
               + ("" if local else alias + "\n")
               + "pub fn main() void { " + (alias if local else "")
               + f"const p: {pointer('u32', destination, True)} = &alias; _ = p.*; }}\n")
    for declaration in ("const value align(64) = 7;", "const value align(64) = u8;",
                        "const value align(64) = target;", "const value align(64) = null;",
                        "const value align(64) = undefined;", "const value align(64) = .tag;",
                        "const value: comptime_int align(64) = 7;", "const value: type align(64) = u8;"):
        yield "fn target() u8 { return 7; }\n" + declaration + "\npub fn main() void { _ = value; }\n"


    # Type/alignment resolution may publish an address without resolving contents.
    for ty, seed in (("u8", "7"), ("i40", "-7"), ("bool", "true"),
                     ("u0", "0"), ("void", "{}"), ("*const u8", "&byte"),
                     ("*const fn () u8", "target")):
        for alignment, first in itertools.product((0, 1, 64, 256), ("value", "address")):
            aligned = f" align({alignment})" if alignment else ""
            entry = "_ = a; _ = b;" if first == "value" else "_ = &a; _ = &b;"
            prefix = "const byte: u8 = 3; fn target() u8 { return 7; }\n"
            for dependency in ("&a == &a", "&b == &b"):
                yield (prefix + f"const a: {ty}{aligned} = if ({dependency}) {seed} else {seed};\n"
                       + f"const b: {ty}{aligned} = if (&a == &a) {seed} else {seed};\n"
                       + f"pub fn main() void {{ {entry} }}\n")
            # A dereference needs contents, even if the pointer itself was published.
            yield (f"const a: {ty}{aligned} = (&b).*; const b: {ty}{aligned} = (&a).*;\n"
                   + f"pub fn main() void {{ {entry} }}\n")
    for alignment in ("", " align(64)"):
        for declaration in (f"const a: u8{alignment} = 256;",
                            f"const a: u8{alignment} = if (&a == &a) 256 else 0;",
                            f"const a{alignment} = if (&a == &a) @as(u8, 7) else 0;",
                            f"const a: u8{alignment} = (&a).*;",
                            f"const a: u8{alignment} = 7; const b: u8 = a;"):
            yield declaration + "\npub fn main() void { _ = &a; }\n"
    for source in (
            "const a: u8 align(if (&a == &a) 64 else 1) = 7;",
            "const a: u8 align(b) = 7; const b: u29 = if (&a == &a) 64 else 1;",
            "const a: T = 7; const T = if (&a == &a) u8 else u8;",
            "const a: u8 = (&b).*; const b: u8 = 7;",
            "const a: u8 = if (&a == &a) 7 else 0; const p: *const u8 = &a; const b: u8 = p.*;",
            "var a: u8 = if (&b == &b) 7 else 0; const b: u8 = if (&a == &a) 9 else 0;",
            "const a: u8 = 7; const p: *const volatile u8 = &a; const b: u8 = p.*;",
            "const a: u8 = 7; const p: *const u8 = &a; const b: u8 = 7; const c: bool = p == &b;",
            "const unused: u8 = 256; const a: u8 = 7;"
    ):
        yield source + "\npub fn main() void { _ = &a; }\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("szc")
    parser.add_argument("zig")
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(prefix="szc-global-const-alignment-diff-") as directory:
        path = Path(directory) / "test.zig"
        for source in cases():
            path.write_text(source)
            szc = run_compiler([args.szc, "tir", str(path)], source,
                f"diff_global_const_alignment case {checked + 1} szc", timeout=120)
            zig = run_compiler([args.zig, "build-exe", str(path), "-fno-emit-bin", "-j2"], source,
                f"diff_global_const_alignment case {checked + 1} zig", timeout=120)
            if szc.returncode != zig.returncode:
                raise SystemExit("disagreement:\n" + source + szc.stderr.decode(errors="replace")
                                 + zig.stderr.decode(errors="replace"))
            checked += 1
    print(f"global constant alignment: {checked} matched Zig 0.17.0", flush=True)


if __name__ == "__main__":
    main()
