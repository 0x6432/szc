# Finite error-set diagnostic renderer regressions

Run the source-level suite without a Zig dependency:

```sh
python3 tests/run_error_set_renderer_20261022.py build/szc
```

Explicitly revalidate its independent expected bytes with the pinned reference:

```sh
python3 tests/run_error_set_renderer_20261022.py build/szc \
  --zig /data/toolchain/zig-reference/zig-x86_64-linux-0.17.0/zig \
  --report /tmp/renderer-results.json
```

`--reference-only --zig /path/to/zig` validates expectations without a candidate.
The runner rejects a reference whose `version` is not exactly `0.17.0`.
It never discovers Zig implicitly, requires no QBE, and never executes fixtures.
All compiler invocations use the existing bounded process-group helper; crashes,
loader errors, timeouts, extra primaries, generated stdout, and non-1 rejection
status fail. Compiler hashes before/after detect a binary changed during a run.
Each record contains the exact command, source bytes/hash, primary bytes and
full stdout/stderr bytes in hex. Private source/cache trees are deleted on exit.

## Source-level coverage (32 programs)

- 25 discarded comptime singleton errors: ordinary, quoted and escape-normalized
  identifiers, underscore, keyword, primitive type name, spaces, quote,
  backslash, comma/brace/apostrophe, C0 control, tab, CR, LF, consecutive LF,
  DEL, literal/escaped UTF8, Unicode line separator, three invalid UTF8 byte
  cases, and 600/4096-byte names.
- Seven finite-set implicit coercion failures: forward/reverse input ordering,
  merged sets, quoted/escaped aliases, empty destination, and 600/4096-byte
  destination names. Mixed ordering includes byte01 and byteff; expectations
  are explicit canonical raw-byte order, not a sort of observed candidate text.

Every case requires its full exact primary, including the suffix after a long
name. This catches the former 511-byte sema diagnostic cap, as well as quoting,
escaping, Unicode replacement, lost delimiters and ordering regressions.

### LF presentation is not renderer escaping

Zig0.17 emits the raw decoded LF, then pads the following continuation line to
the first primary-text column. The candidate's existing diagnostic printer
emits the raw LF without that padding. The runner does **not** normalize either
observed output: for reference only, it expands each expected LF by the measured
primary-prefix width. It then compares full bytes. CR, tab, invalid UTF8 and
Unicode separators remain untouched. The unchanged offending source line is
the message delimiter, rather than `splitlines()` or a single-line regex.
This is renderer-byte parity, not global diagnostic presentation parity.

## Obsolete whitebox policy (documented replacement)

`error_set_type_name_20261019.c` previously asserted a different internal policy:
all members quoted as `@"..."`, controls/high bytes hex-escaped, ASCII-only output.
Independent Zig0.17 ABI diagnostics and the source-level suite above show that
finite error-set type names instead concatenate decoded raw member bytes between
`error{` and `}`. Keywords and names that require quotes in Zig source are still
unquoted in diagnostic type names. Thus all seven nonempty original expected
strings and the ASCII-only assertion are genuinely obsolete. Their original
member input bytes and the empty-set input are unchanged. No frozen Zig source
fixture or manifest is changed by this patch.

The existing normal+sanitized shell runner remains the entry point. Its private
source overlay and one-inclusion `sema.c` architecture are preserved. It now runs
14 direct-metadata vectors: the original eight, plus keyword, space, LF, CR/tab,
4096-byte name, and embedded-NUL safety. NUL is rejected in source; its metadata
placeholder `error{<invalid NUL in name>}` is an explicit defensive C-string API
contract, **not** claimed as Zig source-language parity.

```sh
CC=cc sh tests/error_set_type_name_20261019.sh
# Optional immutable four-file overlay:
CC=cc sh tests/error_set_type_name_20261019.sh . /path/to/frozen-src
```

Parent integration owns Makefile/gate wiring. This patch changes tests only.
