#!/usr/bin/env python3
"""Generate mutable global and global-pointer checks at every integer width."""
import argparse
from pathlib import Path


def generate():
    lines = ["// exit: 0"]
    calls = []
    for signed in (False, True):
        for bits in range(1, 65):
            name = ("i" if signed else "u") + str(bits)
            minimum = -(1 << (bits - 1)) if signed else 0
            maximum = (1 << (bits - int(signed))) - 1
            lines += [
                f"var value_{name}: {name} = {minimum};",
                f"var pointer_{name}: *{name} = &value_{name};",
                f"const live_{name} = &value_{name};",
                f"fn load_{name}(p: *const {name}) {name} {{ return p.*; }}",
                f"fn store_{name}(p: *{name}, n: {name}) void {{ p.* = n; }}",
                f"fn check_{name}() u8 {{",
                f"    if (value_{name} != {minimum}) return 1;",
                f"    if (pointer_{name} != live_{name}) return 1;",
                f"    store_{name}(pointer_{name}, {maximum});",
                f"    if (value_{name} != {maximum} or load_{name}(live_{name}) != {maximum}) return 1;",
                f"    value_{name} = {minimum};",
                f"    if (pointer_{name}.* != {minimum}) return 1;",
                "    return 0;",
                "}",
            ]
            calls.append(f"    errors += check_{name}();")
    lines += ["pub fn main() u8 {", "    var errors: u16 = 0;"]
    return "\n".join(lines + calls + ["    return @intCast(errors);", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
