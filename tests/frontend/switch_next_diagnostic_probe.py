#!/usr/bin/env python3
"""Strict focused switch primaries against exact Zig 0.17.0; no case filtering."""
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

CASES = [
 ('index_runtime', 'export fn probe(x:u8)u8{return switch(x){1=>|v,i| v+i,else=>0};}', "cannot capture tag of non-union type 'u8'"),
 ('index_else', 'export fn probe(x:u8)u8{return switch(x){else=>|v,i| v+i};}', "cannot capture tag of non-union type 'u8'"),
 ('index_known_selected', 'export fn probe()u8{return switch(@as(u8,1)){1=>|v,i| v+i,else=>0};}', "cannot capture tag of non-union type 'u8'"),
 ('index_known_unselected', 'export fn probe()u8{return switch(@as(u8,0)){1=>|v,i| v+i,else=>0};}', None),
 ('index_known_else_unselected', 'export fn probe()u8{return switch(@as(u8,0)){0=>0,else=>|v,i| v+i};}', None),
 ('shadow_parameter', 'export fn probe(x:u8)u8{return switch(x){1=>|x| x,else=>0};}', "capture 'x' shadows function parameter from outer scope"),
 ('shadow_parameter_known_unselected', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|x| x,else=>0};}', "capture 'x' shadows function parameter from outer scope"),
 ('underscore_u8', 'export fn probe(x:u8)u8{return switch(x){_=>1,else=>0};}', "'_' prong only allowed when switching on non-exhaustive enums"),
 ('underscore_known_unselected', 'export fn probe()u8{return switch(@as(u8,0)){0=>0,_=>1,else=>2};}', "'_' prong only allowed when switching on non-exhaustive enums"),
 ('underscore_exhaustive_enum', 'const E=enum(u8){a=0,b=1}; export fn probe(x:u8)u8{const e:E=@enumFromInt(x);return switch(e){_=>1,else=>0};}', "'_' prong only allowed when switching on non-exhaustive enums"),
 ('quoted_underscore_tag', 'const E=enum(u8){@"_"=0,a=1}; export fn probe(x:u8)u8{const e:E=@enumFromInt(x);return switch(e){.@"_"=>1,.a=>0};}', None),
 ('nonexhaustive_enum_nyi', 'const E=enum(u8){a=0,_}; export fn probe(x:u8)u8{const e:E=@enumFromInt(x);return switch(e){.a=>1,_=>0};}', None),
 ('index_unselected_undeclared', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| v+i+missing,else=>0};}', "use of undeclared identifier 'missing'"),
 ('index_unselected_unused_payload', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| i,else=>0};}', 'unused capture'),
 ('index_unselected_unused_index', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| v,else=>0};}', 'unused switch tag capture'),
 ('index_unselected_unused_both', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| 1,else=>0};}', 'unused switch tag capture'),
 ('index_unselected_duplicate_bindings', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,v| v,else=>0};}', "redeclaration of capture 'v'"),
 ('index_unselected_shadow_parameter', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|x,i| x+i,else=>0};}', "capture 'x' shadows function parameter from outer scope"),
 ('index_unselected_index_shadow_parameter', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,x| v+x,else=>0};}', "switch tag capture 'x' shadows function parameter from outer scope"),
 ('index_unselected_nested_shadow', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| blk:{const v:u8=1;break :blk v+i;},else=>0};}', "local constant 'v' shadows capture from outer scope"),
 ('index_unselected_nested_duplicate', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| blk:{const z:u8=1;const z:u8=2;break :blk v+i+z;},else=>0};}', "redeclaration of local constant 'z'"),
 ('index_unselected_nested_bad_field', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,i| blk:{const S=struct{a:u8,a:u8};_=S;break :blk v+i;},else=>0};}', "duplicate struct member name 'a'"),
 ('index_unselected_discard_index', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,_| v,else=>0};}', 'discard of tag capture; omit it instead'),
 ('index_unselected_quoted_index', 'export fn probe(x:u8)u8{_=x;return switch(@as(u8,0)){1=>|v,@"_"| v+@"_",else=>0};}', None),
 ('index_original_protocol_unused', 'export fn probe(x: u8) u8 { return switch (x) { 1 => |v, idx| v, else => 0 }; }', 'unused switch tag capture'),

]

def primary(result):
    match = re.search(r'(?m)^.*?:\d+:\d+: error: (.*)$', result.stderr.decode(errors='replace'))
    return match.group(1) if match else None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    parser.add_argument('--candidate', help='combined frontend+sema compiler; omitted for reference-only probes')
    parser.add_argument('--report', required=True)
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    if zig is None: parser.error('reference Zig executable not found')
    version = run([zig, 'version'], timeout=30)
    if version.returncode != 0 or version.stdout != b'0.17.0\n':
        raise SystemExit('exact Zig 0.17.0 required')
    candidate = str(Path(args.candidate).resolve()) if args.candidate else None
    records = []
    with tempfile.TemporaryDirectory(prefix='szc-switch-next-frontend-') as temporary:
        root = Path(temporary)
        for name, body, expected in CASES:
            source = body + '\npub fn main()void{}\n'
            path = root / (name + '.zig')
            path.write_text(source)
            commands = [('reference', [zig, 'build-exe', str(path), '-fno-emit-bin', '--cache-dir', str(root/'local'), '--global-cache-dir', str(root/'global')])]
            if candidate: commands.append(('candidate', [candidate, 'tir', str(path)]))
            record = {'case': name, 'source': source, 'expected_primary': expected, 'results': {}}
            for label, argv in commands:
                result = run(argv, timeout=30)
                text = primary(result)
                if label == 'candidate' and name == 'nonexhaustive_enum_nyi':
                    ok = result.returncode == 1 and text == 'not supported by szc yet: non-exhaustive enums'
                else:
                    ok = (result.returncode == (0 if expected is None else 1) and text == expected)
                record['results'][label] = {'argv': argv, 'exit': result.returncode, 'primary': text, 'pass': ok,
                    'stdout': result.stdout.decode(errors='replace'), 'stderr': result.stderr.decode(errors='replace')}
                print(('PASS' if ok else 'FAIL'), label, name, repr(text), flush=True)
            records.append(record)
    report = {'zig_version': version.stdout.decode(), 'candidate': candidate, 'cases': records}
    Path(args.report).write_text(json.dumps(report, indent=2)+'\n')
    return int(any(not result['pass'] for record in records for result in record['results'].values()))

if __name__ == '__main__':
    raise SystemExit(main())
