#!/usr/bin/env python3
"""Generate native scalar memcpy tests for every supported integer width."""
from pathlib import Path
import sys


def generate():
    declarations, checks = [], []
    for signed in (False, True):
        for bits in range(1, 65):
            ty = ('i' if signed else 'u') + str(bits)
            low = -(1 << (bits - 1)) if signed else 0
            high = (1 << (bits - int(signed))) - 1
            declarations.append(f'fn copy_{ty}(d:[]{ty},s:[]const {ty}) void {{ @memcpy(d,s); }}')
            checks.extend([
                f'var d_{ty}:[3]{ty}=undefined;',
                f'const s_{ty}:[3]{ty}=.{{{low},{high},0}};',
                f'copy_{ty}(&d_{ty},&s_{ty});',
                f'if(d_{ty}[0]!={low} or d_{ty}[1]!={high} or d_{ty}[2]!=0) return 1;',
            ])
    return ('// exit: 0\n' + '\n'.join(declarations) + '\npub fn main() u8 {\n'
            + '\n'.join('\t'+line for line in checks) + '\n\treturn 0;\n}\n')


if __name__ == '__main__':
    Path(sys.argv[1]).write_text(generate())
