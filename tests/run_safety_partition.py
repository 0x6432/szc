#!/usr/bin/env python3
"""Prove safety shards retain every case and validate CLI bounds before execution."""
from collections import Counter
from contextlib import redirect_stderr
import io
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0,str(Path(__file__).resolve().parent))
import run_safety as safety


def main():
    baseline = list(safety.cases())
    checks = 0
    for count in (1,2,4,7):
        shards = [list(safety.selected_cases(index,count)) for index in range(count)]
        assert Counter(case for shard in shards for case in shard) == Counter(baseline)
        assert all(shard == baseline[index::count] for index,shard in enumerate(shards))
        assert max(map(len,shards))-min(map(len,shards)) <= 1
        checks += 3
    defaults = safety.arguments([])
    assert (defaults.szc,defaults.shard_index,defaults.shard_count) == ('build/asan/szc',0,1)
    checks += 1
    explicit = safety.arguments(['compiler','--shard-index','3','--shard-count','4'])
    assert (explicit.szc,explicit.shard_index,explicit.shard_count) == ('compiler',3,4)
    checks += 1
    for index,count in ((-1,4),(4,4),(0,0),(0,-1)):
        try:
            with redirect_stderr(io.StringIO()):
                safety.arguments(['--shard-index',str(index),'--shard-count',str(count)])
        except SystemExit as error:
            assert error.code == 2
        else:
            raise AssertionError('invalid shard accepted')
        try:
            safety.selected_cases(index,count)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid selection accepted')
        checks += 2
    print(f'safety partition: {checks} passed; {len(baseline)} cases retained exactly once')


if __name__ == '__main__':
    main()
