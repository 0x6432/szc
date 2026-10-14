#!/usr/bin/env python3
"""Observe actual volatile reads/writes via Linux/x86_64 page faults and single-step."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

TYPES = ('u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64', 'u3', 'i5',
         'u17', 'i33', 'u40', 'i63', 'bool', '*u8', '*const fn () u8')


def fixtures():
    declarations, checks = [], []
    for index, ty in enumerate(TYPES):
        declarations.append(f'extern fn memory_{index}(offset: usize) *align(1) volatile {ty};')
        first = 'true' if ty == 'bool' else '&left' if ty == '*u8' else 'seven' if 'fn' in ty else '-3' if ty.startswith('i') else '3'
        second = 'false' if ty == 'bool' else '&right' if ty == '*u8' else 'nine' if 'fn' in ty else '-4' if ty.startswith('i') else '4'
        for offset in (0, 1):
            name = f'p_{index}_{offset}'
            checks.extend([f'const {name} = memory_{index}({offset});',
                           f'{name}.* = {first};', f'{name}.* = {second};',
                           f'_ = {name}.*;', f'_ = {name}.*;', f'{name}.* = {first};',
                           f'const value_{index}_{offset} = {name}.*;',
                           'if (!observe_finish(3, 3)) return 1;'])
            if 'fn' in ty:
                checks.append(f'if (value_{index}_{offset}() != 7) return 2;')
            else:
                checks.append(f'if (value_{index}_{offset} != {first}) return 2;')
    for index, (ty, value) in enumerate((('u0', '0'), ('void', '{}')), len(TYPES)):
        declarations.append(f'extern fn memory_{index}(offset: usize) *volatile {ty};')
        for offset in (0, 1):
            checks.extend([f'const p_{index}_{offset} = memory_{index}({offset});',
                           f'p_{index}_{offset}.* = {value};', f'_ = p_{index}_{offset}.*;',
                           'if (!observe_finish(0, 0)) return 3;'])
    for offset in (0, 1):
        checks.extend([
            f'const loop_{offset} = memory_0({offset});',
            f'loop_{offset}.* = 7;',
            f'for (0..4) |_| {{ _ = loop_{offset}.*; }}',
            'if (!observe_finish(4, 1)) return 4;',
            f'const compound_{offset} = memory_0({offset});',
            f'compound_{offset}.* = 7;', f'compound_{offset}.* += 1;',
            f'const compound_value_{offset} = compound_{offset}.*;',
            'if (!observe_finish_compound()) return 5;',
            f'if (compound_value_{offset} != 8) return 6;',
            f'const cancel_{offset} = memory_0({offset});',
            f'if (&cancel_{offset}.* != cancel_{offset}) return 8;',
            'if (!observe_finish(0, 0)) return 9;',
        ])
    source = ('extern fn observe_finish(reads: u32, writes: u32) bool;\n'
              'extern fn observe_finish_compound() bool;\n'
              'var left: u8 = 1;\nvar right: u8 = 2;\n'
              'fn seven() u8 { return 7; }\nfn nine() u8 { return 9; }\n'
              + '\n'.join(declarations) + '\npub fn main() u8 {\n' + '\n'.join(checks) + '\nreturn 0;\n}\n')
    aliases = []
    for index in range(len(TYPES) + 2):
        aliases.extend(['.text', f'.globl memory_{index}', f'memory_{index}:', 'jmp observe_memory'])
    assembly = '\n'.join(aliases) + '\n.section .note.GNU-stack,"",@progbits\n'
    return source, assembly


def run(command, env=None):
    result = subprocess.run(command, capture_output=True, env=env, timeout=120)
    assert result.returncode == 0, (command, result.returncode, result.stderr.decode(errors='replace'))
    return result


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get('SZC_CC', 'cc'))
    assert cc, 'system C compiler not found'
    helper = Path(__file__).resolve().parent / 'helpers' / 'volatile_observer.c'
    with tempfile.TemporaryDirectory(prefix='szc-volatile-access-') as directory:
        root = Path(directory)
        source, assembly, object_ = root / 'main.zig', root / 'aliases.s', root / 'observer.o'
        text, asm = fixtures()
        source.write_text(text)
        assembly.write_text(asm)
        run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-c', str(helper), '-o', str(object_)])
        wrapper = root / 'cc-observer'
        wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(cc) + ' "$@" ' + shlex.quote(str(assembly)) + ' ' + shlex.quote(str(object_)) + '\n')
        wrapper.chmod(0o755)
        executable = root / 'native'
        run([compiler, 'build-exe', str(source), '-o', str(executable), '--keep-temps'],
            dict(os.environ, SZC_CC=str(wrapper)))
        result = run([str(executable)])
        assert not result.stdout and not result.stderr
        if os.environ.get('ZIG'):
            for mode in ('Debug', 'ReleaseFast'):
                executable = root / ('reference-' + mode)
                run([os.environ['ZIG'], 'build-exe', str(source), str(assembly), str(object_),
                     '-O' + mode, '-j2', '-lc', f'-femit-bin={executable}'])
                result = run([str(executable)])
                assert not result.stdout and not result.stderr
    print(f'volatile accesses: {2 * (len(TYPES) + 5)} observed sequences, 0 failed', flush=True)


if __name__ == '__main__':
    main()
