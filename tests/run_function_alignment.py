#!/usr/bin/env python3
"""Check real internal/export/extern function alignment through QBE and ELF."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ALIGNMENTS = (1, 2, 4, 8, 16, 32, 64, 256, 4096)


def fixtures():
    declarations = []
    checks = []
    assembly = ['.text', 'check_slot:', 'mov (%rdi), %rdi', 'check:', 'mov %rsi, %rax', 'sub $1, %rax',
                'and %rdi, %rax', 'sete %al', 'movzbl %al, %eax', 'ret']
    for index, alignment in enumerate(ALIGNMENTS):
        declarations.extend([
            f'extern fn check_{index}(p: *const *align({alignment}) const fn () u8, alignment: usize) bool;',
            f'fn internal_{index}() align({alignment}) u8 {{ return 7; }}',
            f'export fn exported_{index}() align({alignment}) u8 {{ return 8; }}',
            f'extern fn external_{index}() align({alignment}) u8;',
            f'extern fn check_exported_{index}() bool;',
            f'extern fn check_external_{index}() bool;',
            f'const slot_{index}: *align({alignment}) const fn () u8 = &internal_{index};'])
        checks.extend([f'if (!check_{index}(&slot_{index}, {alignment})) return 1;',
                       f'if (slot_{index}() != 7) return 2;',
                       f'if (!check_exported_{index}() or !check_external_{index}()) return 3;',
                       f'if (exported_{index}() != 8 or external_{index}() != 9) return 4;'])
        assembly.extend([f'.globl check_{index}', f'.set check_{index}, check_slot', f'.balign {alignment}', f'.globl external_{index}',
                         f'external_{index}:', 'mov $9, %eax', 'ret'])
        for kind in ('exported', 'external'):
            assembly.extend([f'.globl check_{kind}_{index}', f'check_{kind}_{index}:',
                             f'lea {kind}_{index}(%rip), %rdi', f'mov ${alignment}, %esi', 'jmp check'])
    assembly.append('.section .note.GNU-stack,"",@progbits')
    source = '\n'.join(declarations) + '\npub fn main() u8 {\n' + '\n'.join(checks) + '\nreturn 0;\n}\n'
    return source, '\n'.join(assembly) + '\n'


def run(command, env=None):
    result = subprocess.run(command, capture_output=True, timeout=120, env=env)
    assert result.returncode == 0, (command, result.returncode, result.stderr.decode(errors='replace'))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get('SZC_CC', 'cc'))
    assert cc, 'system C compiler not found'
    with tempfile.TemporaryDirectory(prefix='szc-function-alignment-') as directory:
        root = Path(directory)
        source, assembly = root / 'main.zig', root / 'helpers.s'
        text, asm = fixtures()
        source.write_text(text)
        assembly.write_text(asm)
        wrapper = root / 'cc-helpers'
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(cc) + ' "$@" ' + shlex.quote(str(assembly)) + '\n')
        wrapper.chmod(0o755)
        executable = root / 'native'
        run([compiler, 'build-exe', str(source), '-o', str(executable), '--keep-temps'],
            dict(os.environ, SZC_CC=str(wrapper)))
        assert executable.stat().st_size < 100000, 'function alignment introduced excessive section padding'
        assert b'TEXTREL' not in run(['readelf', '-d', str(executable)]).stdout
        assert b'AX' in run(['readelf', '-SW', str(executable)]).stdout
        result = run([str(executable)])
        assert not result.stdout and not result.stderr
        standalone = subprocess.run([compiler, 'emit-qbe', str(source)], capture_output=True, timeout=120)
        assert standalone.returncode == 1
        assert b'function alignment above 16 requires assembly output' in standalone.stderr
        if os.environ.get('ZIG'):
            executable = root / 'reference'
            run([os.environ['ZIG'], 'build-exe', str(source), str(assembly), f'-femit-bin={executable}'])
            result = run([str(executable)])
            assert not result.stdout and not result.stderr
    print(f'function alignment: {3 * len(ALIGNMENTS)} passed, 0 failed', flush=True)


if __name__ == '__main__':
    main()
