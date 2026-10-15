#!/usr/bin/env python3
"""Check two-word descriptors against C, including foreign updates and snapshots."""
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
    with tempfile.TemporaryDirectory(prefix='szc-slice-storage-') as directory:
        root = Path(directory)
        helper = root/'foreign.c'
        helper.write_text('#include <stdint.h>\n#include <string.h>\n'
                          'uint8_t check(const void *s,const void *p,uintptr_t n) {\n'
                          ' uintptr_t words[2]; memcpy(words,s,sizeof words);\n'
                          ' return words[0] == (uintptr_t)p && words[1] == n ? 0 : 1; }\n'
                          'uint8_t check_fields(const void *s,const void *p,const void *n) {\n'
                          ' return (uintptr_t)p == (uintptr_t)s && (uintptr_t)n == (uintptr_t)s+8 ? 0 : 1; }\n'
                          'void replace(void *s,void *p,uintptr_t n) {\n'
                          ' uintptr_t words[2] = {(uintptr_t)p,n}; memcpy(s,words,sizeof words); }\n')
        wrapper = root/'cc-pie'
        wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(cc)+' -fPIE -pie -Wl,-z,relro,-z,now "$@" '+shlex.quote(str(helper))+'\n')
        wrapper.chmod(0o755)
        env = dict(os.environ,SZC_CC=str(wrapper))
        for alignment in (1,2,8,256):
            source = root/'test.zig'
            source.write_text('var a: [3]u16 = .{7,9,11}; var b: [2]u16 = .{13,15};\n'
                              f'var current: []u16 align({alignment}) = &a;\n'
                              'var middle: []u16 = a[1..3];\n'
                              'const data_slot = &current.ptr; const length_slot = &current.len;\n'
                              'extern fn check(s: *align(1) const []u16,p: [*]u16,n: usize) u8;\n'
                              'extern fn check_fields(s: *align(1) const []u16,p: *align(1) const [*]u16,n: *align(1) const usize) u8;\n'
                              'extern fn replace(s: *align(1) []u16,p: [*]u16,n: usize) void;\n'
                              'fn snapshot(s: []u16) u16 { replace(&current,&b,2); return s[0]+@as(u16,@intCast(s.len)); }\n'
                              'pub fn main() u8 { if (check(&current,&a,3) != 0) return 1;\n'
                              ' if (check_fields(&current,data_slot,length_slot) != 0) return 10;\n'
                              ' if (snapshot(current) != 10) return 2;\n'
                              ' if (current.len != 2 or current[0] != 13) return 3;\n'
                              ' if (check(&current,&b,2) != 0) return 4;\n'
                              ' current[1] = 17; if (b[1] != 17) return 5;\n'
                              ' if (check(&middle,middle.ptr,2) != 0 or middle[0] != 9) return 6;\n'
                              ' middle[1] = 19; if (a[2] != 19) return 7;\n'
                              ' const cut: []u16 = current[1..2];\n'
                              ' if (check(&cut,current.ptr + 1,1) != 0 or cut[0] != 17) return 8;\n'
                              ' cut[0] = 23; if (b[1] != 23) return 9; return 0; }\n')
            executable = root/'native'
            run([compiler,'build-exe',str(source),'-o',str(executable)],env)
            assert run([str(executable)]) == b''
            assert 'DYN' in run(['readelf','-h',str(executable)]).decode()
            assert b'TEXTREL' not in run(['readelf','-d',str(executable)])
            zig = os.environ.get('ZIG')
            if zig:
                reference = root/'reference'
                run([zig,'build-exe',str(source),str(helper),'-lc','-j2',f'-femit-bin={reference}'],timeout=120)
                assert run([str(reference)]) == b''
    print('slice storage: 4 passed, 0 failed')


if __name__ == '__main__':
    main()
