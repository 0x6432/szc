#!/usr/bin/env python3
"""Generate scalar-width readbacks from compile-time arrays, without aggregate ABI use."""
from pathlib import Path
import sys


def generate():
    checks = []
    for signed in (False, True):
        for bits in (1, 3, 7, 8, 16, 17, 31, 32, 33, 40, 63, 64):
            ty = ("i" if signed else "u") + str(bits)
            seed = -1 if signed else 1
            name = f"a_{ty}"
            checks.extend((f"const {name} = [_]{ty}{{{seed}, 0, {seed}}};",
                           f"if ({name}.len != 3 or {name}[0] != {seed} or {name}[1] != 0) return 1;",
                           f"const b_{ty}: [3]{ty} = .{{{seed}, 0, {seed}}};",
                           f"if (({name} ++ b_{ty}).len != 6 or ({name} ++ b_{ty})[5] != {seed}) return 2;",
                           f"if (([_]{ty}{{}} ++ {name})[2] != {seed}) return 3;"))
    checks.extend(("const zero = [_]u0{0, 0}; if (zero[1] != 0) return 4;",
                   "const empty = [_]void{{}, {}}; _ = empty[1];",
                   "const flags = [_]bool{true, false}; if (!flags[0] or flags[1]) return 5;"))
    return "// exit: 0\npub fn main() u8 {\n" + "\n".join(checks) + "\nreturn 0;\n}\n"


if __name__ == "__main__":
    Path(sys.argv[1]).write_text(generate())
