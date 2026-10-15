#!/usr/bin/env python3
"""Check physical scalar sentinel array layout and interior relocations against C."""
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
    with tempfile.TemporaryDirectory(prefix='szc-sentinel-array-storage-') as directory:
        root = Path(directory)
        helper = root/'foreign.c'
        helper.write_text('#include <stdint.h>\n#include <string.h>\n'
                          'uint8_t check(const void *p,const void *last,const void *empty,const void *flags,uintptr_t alignment) {\n'
                          ' uint16_t a[6],e; unsigned char b[2];\n'
                          ' if ((uintptr_t)p % alignment || (uintptr_t)last != (uintptr_t)p+10) return 1;\n'
                          ' memcpy(a,p,sizeof a); memcpy(&e,empty,sizeof e); memcpy(b,flags,sizeof b);\n'
                          ' if (a[0]!=1 || a[1]!=2 || a[2]!=65535 || a[3]!=3 || a[4]!=4 || a[5]!=65535) return 2;\n'
                          ' return e==99 && b[0]==0 && b[1]==1 ? 0 : 3; }\n')
        wrapper = root/'cc-pie'
        wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(cc)+' -fPIE -pie -Wl,-z,relro,-z,now "$@" '+shlex.quote(str(helper))+'\n')
        wrapper.chmod(0o755)
        env = dict(os.environ,SZC_CC=str(wrapper))
        for alignment in (1,2,16,256):
            source = root/'test.zig'
            source.write_text(f'var a: [2][2:65535]u16 align({alignment}) = .{{.{{1,2}},.{{3,4}}}};\n'
                              f'var empty: [0:99]u16 align({alignment}) = .{{}};\n'
                              'const flags = [1:true]bool{false};\n'
                              'const last = &a[1][2];\n'
                              'extern fn check(p: *align(1) const [2][2:65535]u16,last_arg: *align(1) const u16,empty_arg: *align(1) const [0:99]u16,flags_arg: *const [1:true]bool,alignment: usize) u8;\n'
                              f'pub fn main() u8 {{ return check(&a,last,&empty,&flags,{alignment}); }}\n')
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
    print('sentinel array storage: 4 passed, 0 failed')


if __name__ == '__main__':
    main()
