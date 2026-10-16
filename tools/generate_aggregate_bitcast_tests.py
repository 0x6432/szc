#!/usr/bin/env python3
"""Generate executed logical-array bit-cast checks across native bit widths."""
from pathlib import Path
import sys


def generate(lengths=(1, 2, 3, 4, 5, 8)):
    declarations = ['// exit: 0']
    checks = []
    number = 0
    for length in lengths:
        for width in range(1, 64 // length + 1):
            for signed in (False, True):
                number += 1
                bits = length * width
                child = ('i' if signed else 'u') + str(width)
                array = f'[{length}]{child}'
                declarations += [
                    f'fn pack_{number}(n:{array}) u{bits} {{ return @bitCast(n); }}',
                    f'fn unpack_{number}(n:u{bits}) {array} {{ return @bitCast(n); }}',
                ]
                mask = (1 << width) - 1
                for raw in sorted({0, (1 << bits) - 1, (1 << (bits - 1)) | 1}):
                    values = [(raw >> (i * width)) & mask for i in range(length)]
                    if signed:
                        values = [v - (1 << width) if v >= (1 << (width - 1)) else v for v in values]
                    initializer = '.{' + ','.join(map(str, values)) + '}'
                    checks.append(f'    if(pack_{number}({initializer})!={raw}) return 1;')
                    checks.append(f'    const a_{number}_{raw}=unpack_{number}({raw});')
                    condition = ' or '.join(f'a_{number}_{raw}[{i}]!={v}' for i, v in enumerate(values))
                    checks.append(f'    if({condition}) return 2;')
    declarations.append('pub fn main() u8 {')
    declarations.extend(checks)
    declarations.extend(['    return 0;', '}'])
    return '\n'.join(declarations) + '\n'


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate((int(sys.argv[2]),) if len(sys.argv) > 2 else (1, 2, 3, 4, 5, 8)))
