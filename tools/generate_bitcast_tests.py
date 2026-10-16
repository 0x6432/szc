#!/usr/bin/env python3
"""Generate runtime signed/unsigned bit-cast checks at every native integer width."""
from pathlib import Path
import sys


def generate():
    lines = ['// exit: 0']
    for width in range(1, 65):
        lines.append(f'fn signed_{width}(n: u{width}) i{width} {{ return @bitCast(n); }}')
        lines.append(f'fn unsigned_{width}(n: i{width}) u{width} {{ return @bitCast(n); }}')
        lines.append(f'fn extended_{width}(n: u{width}) i64 {{ const x: i{width} = @bitCast(n); return x; }}')
    lines.append('pub fn main() u8 {')
    for width in range(1, 65):
        modulus = 1 << width
        for value in sorted({0, 1, modulus//2-1, modulus//2, modulus-1}):
            expected = value if value < modulus//2 else value-modulus
            lines.append(f'    if (signed_{width}({value}) != {expected}) return 1;')
            lines.append(f'    if (unsigned_{width}({expected}) != {value}) return 2;')
            lines.append(f'    if (extended_{width}({value}) != {expected}) return 3;')
    lines.extend(['    return 0;', '}'])
    return '\n'.join(lines)+'\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
