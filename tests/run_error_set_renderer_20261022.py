#!/usr/bin/env python3
"""Byte-exact source-level finite-error-set diagnostic regressions; never execute.

Candidate-only by default. --zig explicitly revalidates the independently frozen
Zig0.17 expectations. Source programs/caches live in a private temporary tree.
Only Zig's presentation indentation after LF is accounted for, not decoded-name
bytes. No Unicode decoding, splitlines(), replacement characters or truncation.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from compiler_process import run

PREFIX = re.compile(rb'^([^\n]+?:\d+:\d+: error: )', re.M)


def cases():
    # Source spelling and expected decoded bytes are deliberately independent.
    names = [
        ('ordinary', 'Plain', b'Plain'),
        ('quoted-ordinary', '@"Plain"', b'Plain'),
        ('escaped-ordinary', '@"\\x50lain"', b'Plain'),
        ('underscore', '_', b'_'),
        ('quoted-underscore', '@"_"', b'_'),
        ('keyword', '@"error"', b'error'),
        ('primitive', 'u8', b'u8'),
        ('space', '@"two words"', b'two words'),
        ('quote', '@"a\\\"b"', b'a"b'),
        ('backslash', '@"a\\\\b"', b'a\\b'),
        ('delimiters', "@\"a,b}'c\"", b"a,b}'c"),
        ('control-01', '@"\\x01"', b'\x01'),
        ('tab', '@"a\\tb"', b'a\tb'),
        ('carriage-return', '@"a\\rb"', b'a\rb'),
        ('linefeed', '@"a\\nb"', b'a\nb'),
        ('two-linefeeds', '@"a\\n\\nb"', b'a\n\nb'),
        ('del', '@"\\x7f"', b'\x7f'),
        ('utf8-literal', '@"é😀"', b'\xc3\xa9\xf0\x9f\x98\x80'),
        ('utf8-escaped', '@"\\xc3\\xa9"', b'\xc3\xa9'),
        ('unicode-line-separator', '@"\\xe2\\x80\\xa8"', b'\xe2\x80\xa8'),
        ('invalid-ff', '@"\\xff"', b'\xff'),
        ('invalid-c3', '@"\\xc3"', b'\xc3'),
        ('invalid-utf8-sequence', '@"\\xc3\\x28"', b'\xc3('),
    ]
    for size in (600, 4096):
        names.append((f'long-{size}', 'A' * size, b'A' * size))
    for label, spelling, decoded in names:
        source = ('comptime { error.' + spelling + '; }\npub fn main() void {}\n').encode()
        yield label, source, b"error set of type 'error{" + decoded + b"}' is ignored"
    # Mixed raw-byte ordering, input reversal, quoted plain normalization, merge.
    for label, expression in [
        ('order-forward', 'error{z,@"\\xff",@"a b",@"error",M,_,a,@"\\x01"}'),
        ('order-reverse', 'error{@"\\x01",a,_,M,@"error",@"a b",@"\\xff",z}'),
        ('order-merge', '(error{z,@"\\xff",M,_}||error{a,@"a b",@"error",@"\\x01"})'),
        ('quoted-alias', 'error{@"B",@"\\x41"}'),
        ('empty', 'error{}'),
        ('coerce-long-600', 'error{' + 'A' * 600 + '}'),
        ('coerce-long-4096', 'error{' + 'A' * 4096 + '}'),
    ]:
        decoded = (b'\x01,M,_,a,a b,error,z,\xff' if label.startswith('order-') else
                   b'A,B' if label == 'quoted-alias' else b'' if label == 'empty' else
                   b'A' * (600 if label.endswith('600') else 4096))
        source = ('const E=' + expression + ';\ncomptime { const x:E=error.Missing; _=x; }\npub fn main() void {}\n').encode()
        yield label, source, b"expected type 'error{" + decoded + b"}', found 'error{Missing}'"


def positive(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError('must be finite and positive')
    return number


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(command, source, expected, reference, timeout):
    row = {'command': command, 'expected_primary_hex': expected.hex()}
    try:
        result = run(command, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as error:
        row.update(passed=False, process_error=str(error),
                   cleanup_capture_incomplete=getattr(error, 'cleanup_capture_incomplete', False),
                   cleanup_reap_incomplete=getattr(error, 'cleanup_reap_incomplete', False))
        return row
    row.update(status=result.returncode, stdout_hex=result.stdout.hex(), stderr_hex=result.stderr.hex())
    matches = list(PREFIX.finditer(result.stderr))
    row['primary_count'] = len(matches)
    actual = None
    if len(matches) == 1:
        match = matches[0]
        # Both drivers print the unchanged offending source line immediately
        # after the primary. This delimits LF-containing messages losslessly.
        tail = result.stderr[match.end():]
        for line in source.split(b'\n'):
            if line.startswith(b'comptime '):
                boundary = b'\n' + line + b'\n'
                if boundary in tail:
                    actual = tail.split(boundary, 1)[0]
                break
        if reference:
            # Zig aligns each continuation to the primary's first text column.
            expected = expected.replace(b'\n', b'\n' + b' ' * len(match.group(1)))
    row['expected_formatted_primary_hex'] = expected.hex()
    row['actual_primary_hex'] = None if actual is None else actual.hex()
    row['passed'] = result.returncode == 1 and result.stdout == b'' and len(matches) == 1 and actual == expected
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidate', nargs='?', default='build/szc')
    parser.add_argument('--zig', help='explicit Zig0.17.0 executable; never discovered implicitly')
    parser.add_argument('--reference-only', action='store_true')
    parser.add_argument('--timeout', type=positive, default=30)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if args.reference_only and not args.zig:
        parser.error('--reference-only requires --zig')
    binaries = {}
    if not args.reference_only:
        binaries['candidate'] = Path(args.candidate).resolve()
    if args.zig:
        binaries['reference'] = Path(args.zig).resolve()
    report = {'contract': 'finite-error-set-renderer-source-20261022', 'records': []}
    try:
        before = {label: sha(path) for label, path in binaries.items()}
        report['binary_sha256_before'] = before
        if args.zig:
            version = run([str(binaries['reference']), 'version'], timeout=args.timeout)
            if version.returncode != 0 or version.stdout != b'0.17.0\n':
                raise ValueError('reference must report exactly version0.17.0')
        with tempfile.TemporaryDirectory(prefix='szc-renderer-source-') as directory:
            temp = Path(directory)
            for label, source, expected in cases():
                path = temp / (label + '.zig')
                path.write_bytes(source)
                record = {'case': label, 'source_hex': source.hex(), 'source_sha256': sha(path)}
                for compiler, binary in binaries.items():
                    reference = compiler == 'reference'
                    command = ([str(binary), 'build-obj', str(path), '-j2', '-fno-emit-bin', '--color', 'off',
                                '--cache-dir', str(temp / 'local'), '--global-cache-dir', str(temp / 'global')]
                               if reference else [str(binary), 'emit-qbe', str(path)])
                    row = check(command, source, expected, reference, args.timeout)
                    record[compiler] = row
                    print(f"{'PASS' if row['passed'] else 'FAIL'} {compiler} {label}: {shlex.join(command)}", flush=True)
                report['records'].append(record)
        after = {label: sha(path) for label, path in binaries.items()}
        report['binary_sha256_after'] = after
        if before != after:
            raise ValueError('compiler binary changed during validation')
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report['infrastructure_error'] = str(error)
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    rows = [record[compiler] for record in report['records'] for compiler in binaries if compiler in record]
    failures = sum(not row['passed'] for row in rows)
    print(f"finite error-set source renderer: {len(rows)} checks, {failures} failures")
    return 2 if 'infrastructure_error' in report else int(failures != 0)


if __name__ == '__main__':
    raise SystemExit(main())
