#!/usr/bin/env python3
"""Keep standalone CI partitions at full compiler/stage/mutation coverage."""
from pathlib import Path
import itertools
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def job(text, name):
    match = re.search(r'^  ' + re.escape(name) + r':\n(.*?)(?=^  [\w-]+:\n|\Z)',
                      text, re.M | re.S)
    assert match is not None, name
    return match.group(1)


def main():
    text = (ROOT / '.github/workflows/ci.yml').read_text()
    replay = job(text, 'standalone-fuzz')
    build = job(text, 'build-and-test')
    compilers = re.search(r'^        cc: \[([^]]+)\]$', replay, re.M)
    stages = re.search(r'^        stage: \[([^]]+)\]$', replay, re.M)
    assert compilers is not None and stages is not None
    compilers = [part.strip() for part in compilers.group(1).split(',')]
    stages = [part.strip() for part in stages.group(1).split(',')]
    assert len(compilers) == 2 and set(compilers) == {'gcc', 'clang'}
    assert len(stages) == 4 and set(stages) == {'lexer', 'parser', 'irgen', 'sema'}
    assert '      fail-fast: false\n' in replay
    assert '      max-parallel: 4\n' in replay
    assert '        timeout-minutes: 15\n' in replay
    assert '    needs: standalone-fuzz\n' in build
    assert '    if: always()\n' in build
    assert '        run: test "${{ needs.standalone-fuzz.result }}" = success\n' in build
    assert 'fuzz-standalone' not in build
    command = 'make CC=${{ matrix.cc }} EXTRA_CFLAGS=-Werror FUZZERS=${{ matrix.stage }} fuzz-standalone'
    assert '        run: ' + command + '\n' in replay
    assert '        if: failure()\n' in replay
    assert '          name: standalone-fuzz-input-${{ matrix.cc }}-${{ matrix.stage }}\n' in replay
    assert '          path: build/fuzz-last-${{ matrix.stage }}.bin\n' in replay
    observed = set()
    for compiler, stage in itertools.product(compilers, stages):
        result = subprocess.run(['make', '-Bn', f'CC={compiler}', 'EXTRA_CFLAGS=-Werror',
                                 f'FUZZERS={stage}', 'fuzz-standalone'], cwd=ROOT,
                                text=True, capture_output=True, check=True)
        output = result.stdout
        assert f'for f in {stage}; do ' in output, output
        assert '-save-input build/fuzz-last-$f.bin fuzz/corpus/$f' in output
        assert '-iterations' not in output, output
        drivers = re.findall(r'-o build/asan/fuzz_(\w+) ', output)
        assert drivers == [stage], drivers
        assert '-Werror' in output and '-fsanitize=address,undefined' in output
        observed.add((compiler, stage))
    assert len(observed) == 8
    assert 'uint32_t iterations = 20000;' in (ROOT / 'fuzz/standalone.c').read_text()
    print('standalone fuzz partitions: 8 exact compiler/stage jobs, unchanged 20000 mutations, guarded build checks')


if __name__ == '__main__':
    main()
