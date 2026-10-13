#!/usr/bin/env python3
"""Generate loop/continue/break-result regressions for all 1–64-bit integer types."""
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
                f"fn check_{name}(a: {name}, b: {name}) u8 {{",
                "    var i: u8 = 0;",
                f"    var saved: {name} = a;",
                "    while (i < 3) : (i += 1) {",
                "        if (i == 1) continue;",
                "        saved = if (i == 0) a else b;",
                "    }",
                "    if (saved != b or i != 3) return 1;",
                f"    const result: {name} = while (true) {{",
                "        if (saved == b) break saved;",
                "        return 1;",
                "    };",
                "    if (result != b) return 1;",
                f"    const never: {name} = while (false) {{ break a; }} else b;",
                "    if (never != b) return 1;",
                "    i = 0;",
                f"    const first: {name} = while (i < 3) : (i += 1) {{ break a; }} else b;",
                "    if (first != a or i != 0) return 1;",
                "    return 0;",
                "}",
            ]
            calls.append(f"    errors += check_{name}({minimum}, {maximum});")
    lines += ["pub fn main() u8 {", "    var errors: u8 = 0;"]
    lines += calls + ["    return errors;", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generate())


if __name__ == "__main__":
    main()
