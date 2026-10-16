#!/usr/bin/env python3
"""Exercise absolute-address loads, stores and aligned numeric initializers in C memory."""
import os
from pathlib import Path
import shlex
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
from run_array_storage import run


def main():
    compiler = str(Path(sys.argv[1]).resolve())
    cc = shutil.which(os.environ.get('SZC_CC','cc'))
    assert cc
    with tempfile.TemporaryDirectory(prefix='szc-integer-pointer-storage-') as directory:
        root = Path(directory)
        helper = root/'memory.c'
        helper.write_text('#define _GNU_SOURCE\n#include <sys/mman.h>\n#include <stdint.h>\n'
                          'uint8_t setup(void) { void *p=(void *)(uintptr_t)0x100000000ULL;\n'
                          ' return mmap(p,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==p ? 0 : 1; }\n')
        wrapper = root/'cc-memory'
        wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(cc)+' "$@" '+shlex.quote(str(helper))+'\n')
        wrapper.chmod(0o755)
        for alignment in (1,8,256):
            source = root/'test.zig'
            source.write_text('extern fn setup() u8;\n'
                              'const scalar: *u16 = @ptrFromInt(4294967296);\n'
                              'const array: *[3]u16 = @ptrFromInt(4294967296);\n'
                              'const many: [*]u16 = @ptrFromInt(4294967296);\n'
                              'const constant_slice: [:17]u16 = many[0..2:17];\n'
                              f'var saved: *u16 align({alignment}) = @ptrFromInt(4294967296);\n'
                              'pub fn main() u8 { if (setup() != 0) return 1;\n'
                              ' scalar.* = 7; array[1] = 9; many[2] = 17;\n'
                              ' if (scalar.* != 7 or saved.* != 7 or array[1] != 9 or many[2] != 17) return 2;\n'
                              ' const s: [:17]u16 = many[0..2:17];\n'
                              ' if (s.len != 2 or s[1] != 9 or s[2] != 17) return 3;\n'
                              ' if (constant_slice.len != 2 or constant_slice[1] != 9) return 6;\n'
                              ' const copy = array.*; if (copy[0] != 7 or copy[1] != 9 or copy[2] != 17) return 4;\n'
                              ' const view = many[0..2]; view[1] = 13; if (array[1] != 13) return 5;\n'
                              ' return 0; }\n')
            executable = root/'native'
            run([compiler,'build-exe',str(source),'-o',str(executable)],dict(os.environ,SZC_CC=str(wrapper)))
            assert run([str(executable)]) == b''
            if os.environ.get('ZIG'):
                executable = root/'reference'
                run([os.environ['ZIG'],'build-exe',str(source),str(helper),'-lc','-j2',f'-femit-bin={executable}'],timeout=120)
                assert run([str(executable)]) == b''
    print('integer pointer storage: 3 passed, 0 failed',flush=True)


if __name__ == '__main__':
    main()
