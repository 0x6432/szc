#!/usr/bin/env python3
"""Check strengthened slice descriptor layout, snapshots, and foreign memory."""
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
    with tempfile.TemporaryDirectory(prefix='szc-alignment-cast-storage-') as directory:
        root = Path(directory)
        helper = root/'foreign.c'
        helper.write_text('#include <stdint.h>\n#include <string.h>\n#include <stdbool.h>\n'
                          '_Alignas(256) static uint16_t data[4] = {7,9,11,17};\n'
                          'void prepare(void *s, bool empty) {\n'
                          ' uintptr_t pair[2] = {(uintptr_t)data + (empty ? 1 : 0),empty ? 0 : 3};\n'
                          ' memcpy(s,pair,sizeof pair); }\n'
                          'uint8_t check(const void *s, bool empty) {\n'
                          ' uintptr_t pair[2]; memcpy(pair,s,sizeof pair);\n'
                          ' return pair[0] == (uintptr_t)data + (empty ? 1 : 0) && pair[1] == (empty ? 0 : 3) ? 0 : 1; }\n')
        wrapper = root/'cc-pie'
        wrapper.write_text('#!/bin/sh\nexec '+shlex.quote(cc)+' -fPIE -pie -Wl,-z,relro,-z,now "$@" '+shlex.quote(str(helper))+'\n')
        wrapper.chmod(0o755)
        env = dict(os.environ,SZC_CC=str(wrapper))
        for storage_alignment in (1,8,256):
            for data_alignment in (16,256):
                weak = '[]align(1) const volatile u16'
                strong = f'[]align({data_alignment}) const volatile u16'
                source = root/'test.zig'
                source.write_text(f'var weak: {weak} align({storage_alignment}) = undefined;\n'
                                  f'var strong: {strong} align({storage_alignment}) = undefined;\n'
                                  f'extern fn prepare(p: *align(1) {weak}, empty: bool) void;\n'
                                  f'extern fn check(p: *align(1) const {strong}, empty: bool) u8;\n'
                                  f'fn strengthen(s: {weak}) {strong} {{ return @alignCast(s); }}\n'
                                  f'fn snapshot(s: {strong}) u16 {{ prepare(&weak,true); return s[1]; }}\n'
                                  'pub fn main() u8 { prepare(&weak,false); strong = strengthen(weak);\n'
                                  ' if (check(&strong,false) != 0 or strong.len != 3) return 1;\n'
                                  ' if (snapshot(strong) != 9 or strong.len != 3) return 2;\n'
                                  ' strong = strengthen(weak); if (check(&strong,true) != 0) return 3; return 0; }\n')
                executable = root/'native'
                run([compiler,'build-exe',str(source),'-o',str(executable)],env,timeout=120)
                assert run([str(executable)]) == b''
                assert 'DYN' in run(['readelf','-h',str(executable)]).decode()
                assert b'TEXTREL' not in run(['readelf','-d',str(executable)])
                zig = os.environ.get('ZIG')
                if zig:
                    reference = root/'reference'
                    run([zig,'build-exe',str(source),str(helper),'-lc','-j2',f'-femit-bin={reference}'],timeout=120)
                    assert run([str(reference)]) == b''
    print('alignment cast storage: 6 passed, 0 failed')


if __name__ == '__main__':
    main()
