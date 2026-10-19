#!/usr/bin/env python3
"""Scoped anonymous-namespace comptime ignored-result controls; no filtering."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from compiler_process import run

# None means reference accepts; exact strings mean primary-error parity required.
IGNORED = "value of type 'u8' ignored"
CASES = [
 ('direct', '@as(u8,3);', IGNORED),
 ('nested_block', '{ @as(u8,3); }', IGNORED),
 ('selected_if_block', 'if(true){@as(u8,3);}', IGNORED),
 ('selected_if_value', 'if(true) @as(u8,3);', IGNORED),
 ('selected_if_else_value', 'if(true) @as(u8,3) else @as(u8,4);', IGNORED),
 ('pruned_if_block', 'if(false){@as(u8,3);}', None),
 ('pruned_if_value', 'if(false) @as(u8,3);', None),
 ('selected_switch_block', 'switch(@as(u8,1)){1=>{@as(u8,3);},else=>{}}', IGNORED),
 ('selected_switch_value', 'switch(@as(u8,1)){1=>@as(u8,3),else=>@as(u8,4)}', IGNORED),
 ('pruned_switch_block', 'switch(@as(u8,0)){1=>{@as(u8,3);},else=>{}}', None),
 ('defer_value', 'defer @as(u8,3);', IGNORED),
 ('defer_nested', 'defer {@as(u8,3);}', IGNORED),
 ('defer_selected_if', 'defer {if(true){@as(u8,3);}}', IGNORED),
 ('defer_pruned_if', 'defer {if(false){@as(u8,3);}}', None),
 ('discard_direct', '_ = @as(u8,3);', None),
 ('discard_if', '_ = if(true) @as(u8,3) else @as(u8,4);', None),
 ('discard_switch', '_ = switch(@as(u8,1)){1=>@as(u8,3),else=>@as(u8,4)};', None),
 ('const_if', 'const x:u8=if(true) @as(u8,3) else @as(u8,4);_=x;', None),
 ('const_switch', 'const x:u8=switch(@as(u8,1)){1=>@as(u8,3),else=>@as(u8,4)};_=x;', None),
 ('const_labeled', 'const x:u8=blk:{break :blk @as(u8,3);};_=x;', None),
 ('bad_in_const_labeled', 'const x:u8=blk:{@as(u8,3);break :blk @as(u8,4);};_=x;', IGNORED),
 ('defer_discard', 'defer {_ = @as(u8,3);}', None),
 ('labeled_ignored', 'blk:{break :blk @as(u8,3);}', IGNORED),
 ('void_block', '{}', None),
 ('pruned_semantic_bad_if', 'if(false){@as(u8,false);}', None),
 ('pruned_semantic_bad_switch', 'switch(@as(u8,0)){1=>{@as(u8,false);},else=>{}}', None),
 ('unselected_panic', 'if(false){@panic("unselected");}', None),
 ('defer_after_break', 'blk:{defer @as(u8,3);break :blk;}', IGNORED),
 ('nested_namespace', 'const N=struct{comptime{@as(u8,3);}};_=N;', IGNORED),
 ('typequery_namespace', 'const N=struct{comptime{@as(u8,3);}};_= @TypeOf(N);', IGNORED),
]

def primary(stderr):
    m = re.search(r'(?m)^.*?:\d+:\d+: error: (.*)$', stderr)
    return m.group(1) if m else None

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    p.add_argument('--candidate')
    p.add_argument('--report', required=True)
    a = p.parse_args()
    zig = shutil.which(a.zig)
    if not zig: p.error('reference Zig executable not found')
    v = run([zig, 'version'], timeout=30)
    if v.returncode != 0 or v.stdout != b'0.17.0\n': raise SystemExit('exact Zig 0.17.0 required')
    candidate = str(Path(a.candidate).resolve()) if a.candidate else None
    rows = []
    with tempfile.TemporaryDirectory(prefix='szc-global-comptime-results-') as temporary:
        root = Path(temporary)
        for name, body, expected in CASES:
            source = 'comptime {' + body + '}\npub fn main()void{}\n'
            file = root / (name+'.zig'); file.write_text(source)
            commands = [('reference', [zig,'build-exe',str(file),'-fno-emit-bin','--cache-dir',str(root/'local'),'--global-cache-dir',str(root/'global')])]
            if candidate: commands.append(('candidate',[candidate,'tir',str(file)]))
            row = {'case':name,'source':source,'expected':expected,'results':{}}
            for label, argv in commands:
                result = run(argv, timeout=30)
                error = result.stderr.decode(errors='replace'); actual=primary(error)
                ok = result.returncode == (0 if expected is None else 1) and actual == expected
                row['results'][label]={'argv':argv,'exit':result.returncode,'primary':actual,'pass':ok,'stdout':result.stdout.decode(errors='replace'),'stderr':error}
                print('PASS' if ok else 'FAIL',label,name,repr(actual),flush=True)
            rows.append(row)
    Path(a.report).write_text(json.dumps({'version':v.stdout.decode(),'candidate':candidate,'cases':rows},indent=2)+'\n')
    return int(any(not result['pass'] for row in rows for result in row['results'].values()))

if __name__ == '__main__':
    raise SystemExit(main())
