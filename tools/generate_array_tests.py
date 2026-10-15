#!/usr/bin/env python3
"""Generate array storage, control-flow and call roundtrips across scalar widths."""
from pathlib import Path
import sys


def generate():
    checks = []
    functions = []
    for signed in (False, True):
        for bits in (1, 3, 7, 8, 16, 17, 31, 32, 33, 40, 63, 64):
            ty = ("i" if signed else "u") + str(bits)
            seed = -1 if signed else 1
            name = f"a_{ty}"
            functions.append(f"fn deref_{ty}(p: *const {ty}) {ty} {{ return p.*; }}")
            functions.append(f"fn read_{ty}(p: *const [3]{ty}, n: usize) {ty} {{ return p[n]; }}")
            functions.append(f"fn copy_{ty}(a: [3]{ty}, c: bool) [3]{ty} {{ return if (c) a else .{{0, {seed}, 0}}; }}")
            functions.append(f"fn nested_{ty}(a: [2][3]{ty}) [2][3]{ty} {{ return a; }}")
            checks.extend((f"const {name} = [_]{ty}{{{seed}, 0, {seed}}};",
                           f"if ({name}.len != 3 or {name}[0] != {seed} or {name}[1] != 0) return 1;",
                           f"const b_{ty}: [3]{ty} = .{{{seed}, 0, {seed}}};",
                           f"if (({name} ++ b_{ty}).len != 6 or ({name} ++ b_{ty})[5] != {seed}) return 2;",
                           f"if (([_]{ty}{{}} ++ {name})[2] != {seed}) return 3;",
                           f"if (read_{ty}(&{name}, 0) != {seed} or read_{ty}(&{name}, 1) != 0) return 6;",
                           f"if (copy_{ty}({name}, true)[2] != {seed} or copy_{ty}({name}, false)[1] != {seed}) return 7;",
                           f"if (nested_{ty}(.{{{name}, b_{ty}}})[1][0] != {seed}) return 8;",
                           f"const p_{ty} = &{name}[2]; if (p_{ty}.* != {seed} or p_{ty} == &{name}[1]) return 9;",
                           f"if (deref_{ty}(p_{ty}) != {seed}) return 10;"))
    checks.extend(("const zero = [_]u0{0, 0}; if (zero[1] != 0) return 4;",
                   "const empty = [_]void{{}, {}}; _ = empty[1];",
                   "const flags = [_]bool{true, false}; if (!flags[0] or flags[1]) return 5;"))
    return "// exit: 0\n" + "\n".join(functions) + "\npub fn main() u8 {\n" + "\n".join(checks) + "\nreturn 0;\n}\n"


if __name__ == "__main__":
    Path(sys.argv[1]).write_text(generate())
