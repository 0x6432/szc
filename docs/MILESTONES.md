# szc milestones

Every milestone is small enough to finish, test and commit in one pass.
Each finished milestone records what it can't do yet ("Limitations") and how it
depends on or feeds other stages ("Interactions").

Status: `[x]` done, `[~]` in progress, `[ ]` not started.

## Phase 0: Infrastructure

- [x] **M0.1 Repo skeleton**: Makefile, `.editorconfig`, README, docs (design, style, handoff).
- [x] **M0.2 Arena allocator**: chunked bump allocator, overflow-checked, ASan poisoning; unit tests.
- [x] **M0.3 Diagnostics**: collect errors (file, byte offset, message), print sorted with line:col and a caret.
- [x] **M0.4 Source loading**: read file, NUL-terminate, file table for diagnostics.
- [x] **M0.5 CI**: GitHub Actions: gcc + clang builds with `-Werror`, ASan/UBSan tests, short libFuzzer run, nightly long fuzz.
  - [x] **M0.5a Bounded parallel reference CI**: independent semantic/native/parser suites and aggregate status.
  - [x] **M0.5b Independent native reference suites**: native/storage/alignment/safety partitions with exact coverage.
  - [x] **M0.5c Exact-coverage safety reference partitions**: four balanced jobs, unchanged checks/deadlines.

## Phase 1: Lexer (`src/lexer.c`)

- [x] **M1.1 Token kinds + punctuation/operators**: every 0.17.0 operator token.
- [x] **M1.2 Identifiers, keywords, builtins, `@"..."` identifiers**: keyword lookup by length and first byte (no linear strcmp scan).
- [x] **M1.3 Literals**: number, char, string and multiline string tokens (same rules as the reference tokenizer).
- [x] **M1.4 Comments**: line, doc (`///`) and container doc (`//!`) comments, CRLF, error recovery up to the next newline.
- [x] **M1.5 `szc tokens` dump + golden tests** (port of the reference tokenizer test-suite).
- [x] **M1.6 Lexer fuzzer** (libFuzzer and standalone driver) + seed corpus.

## Phase 2: AST + parser (`src/ast.h`, `src/parser.c`)

- [x] **M2.1 AST node structs + `szc ast` dumper skeleton**; token lookahead buffer in the parser.
- [x] **M2.2 Top level**: ContainerMembers, `const`/`var` decls, FnProto, TestDecl, ComptimeDecl.
- [x] **M2.3 Binary expressions (Pratt)**: one `parse_expr_precedence(min_prec)` driven by an operator table covering BoolOr … MultiplyExpr (the grammar rules are listed in its comment), non-associative comparisons, `catch |e|` payload; PrefixExpr stays recursive descent.
- [x] **M2.4 Primary/suffix expressions**: literals, identifiers, calls, field access, `.*`, `.?`, indexing, slicing, builtin calls.
- [x] **M2.5 Type expressions**: pointers, slices, arrays, optionals, error unions, `fn` types, ptr modifiers.
- [x] **M2.6 Statements + blocks**: var decls, assignments (incl. destructuring), defer/errdefer, labeled blocks.
- [x] **M2.7 Control flow**: if/while/for with payloads, else branches, break/continue/return, labels.
- [x] **M2.8 switch**: prongs, ranges, `inline`, payloads, labeled switch.
- [x] **M2.9 Containers**: struct/enum/union/opaque, fields, `extern`/`packed`, error sets, init lists.
- [x] **M2.10 asm expressions, callconv/align/addrspace/linksection.**
- [x] **M2.11 Error recovery**: keep going after parse errors and collect them all.
- [x] **M2.12 Parser fuzzer + parse the whole of Zig 0.17.0 `lib/std` without errors.**
- [x] **M2.13 Differential tests against `std.zig.Ast`**: node spans over all Zig sources, accept/reject on mutated sources (CI job `differential`).

## Phase 3: First executable (`irgen.c`, `sema.c`, `codegen.c`)

- [x] **M3.1 Untyped IR (ZIR-like) design + `szc ir` dump.**
- [x] **M3.2 irgen for functions, integer literals, `return`, params, locals.**
- [x] **M3.3 Typed IR + minimal sema**: `void`, `bool`, `i32`/`u8`/`usize` etc., coercion of comptime_int literals.
- [x] **M3.4 QBE codegen**: functions, arithmetic, calls; driver runs `qbe` and `cc`.
- [x] **M3.5 End-to-end test harness** (`tests/run/*.zig` check exit code/stdout).
- [x] **M3.6 Arithmetic semantics**: wrapping/saturating ops, division, shifts, safety checks → panic stub.
  - [x] **M3.6a Checked arithmetic and panic stub**: overflow, cast, unsigned division and shift guards.
  - [x] **M3.6b Runtime saturation and explicit division builtins**.

## Phase 4: Control flow

- [x] **M4.1 if/else, comparisons, `and`/`or` short-circuit.**
- [x] **M4.2 while/break/continue, continue expressions, labeled loops.**
- [x] **M4.3 Blocks as expressions, labeled break with value.**
- [x] **M4.4 for over ranges, `defer`.**
  - [x] **M4.4a Runtime range-based for loops**: captures, multiple inputs, length safety, labels and else/values.
  - [x] **M4.4b defer**: scope-exit execution across fallthrough and control transfers.
- [x] **M4.5 unreachable, noreturn, `@panic` minimal runtime.**
  - [x] **M4.5a Safe unreachable and noreturn checks**: safety panic, return/cast diagnostics, terminating calls/joins.
  - [x] **M4.5b Explicit @panic**: first-class literal values and minimal fixed-length message runtime.
  - [x] **M4.5c Dynamic byte-slice panic messages and contextual joins.**

## Phase 5: Memory

- [~] **M5.1 Single pointers, `&`, `.*`, `const` pointers.**
  - [x] **M5.1a Runtime single pointers**: mutable-local addresses, dereference reads/writes, nested const coercions.
  - [x] **M5.1b Addressable constants/parameters/temporaries and pointer identity**: definition-anchored storage, immutable objects and equality.
  - [~] **M5.1c Remaining pointer integration**: mutable globals, function addresses/calls and pointer attributes.
    - [x] **M5.1c1 Internal mutable scalar globals**: lazy type/initializer resolution and writable pointer relocations.
    - [~] **M5.1c2 Function addresses/calls and pointer attributes**.
      - [x] **M5.1c2a Function types, addresses and indirect calls**.
      - [~] **M5.1c2b Pointer attributes**.
        - [x] **M5.1c2b1 Allowzero pointer qualification**.
        - [~] **M5.1c2b2 Pointer alignment**.
          - [x] **M5.1c2b2a Alignment-qualified pointer types and unaligned memory**.
          - [~] **M5.1c2b2b Explicit declaration/storage alignment**.
            - [x] **M5.1c2b2b1 Aligned mutable locals**.
            - [x] **M5.1c2b2b2 Aligned local constants** (known values and runtime initializers).
            - [x] **M5.1c2b2b3a Internal mutable global alignment**.
            - [x] **M5.1c2b2b3b Aligned global constants** (internal, supported runtime-representable types).
            - [x] **M5.1c2b2b3c Typed immutable address dependencies** (separate address/value resolution and read-only storage).
            - [x] **M5.1c2b2b4 Aligned function declarations/addresses** (local explicitly aligned bare-function aliases remain NYI).
        - [x] **M5.1c2b3 Volatile pointer accesses** (supported scalar/bool/single-pointer values).
          - [x] **M5.1c2b3a Volatile pointer qualification/coercion**.
          - [x] **M5.1c2b3b Observable volatile loads/stores**.
- [~] **M5.2 Arrays, indexing, array literals, `++` at comptime** (0.17.0 has no `**`).
  - [x] **M5.2a Array types and compile-time literals** (typed/inferred length, nested result types).
  - [x] **M5.2b Compile-time indexing, `.len` and concatenation** (including literal byte backing).
  - [~] **M5.2c Runtime array storage, copies, indexing and element addresses** (checked bounds and aggregate ABI).
    - [x] **M5.2c1 Immutable array backing and checked scalar runtime reads**.
    - [x] **M5.2c2 Mutable aggregate slots, initialization and copies**.
    - [x] **M5.2c3 Element address provenance and aggregate value/ABI integration**.
      - [x] **M5.2c3a Internal array calls/returns and runtime aggregate joins**.
      - [x] **M5.2c3b General comptime element-pointer provenance** (root/subobject identity, checked offsets and loads).
    - [x] **M5.2c4 Mutable array globals** (lazy initialization, borrowed element accesses and runtime snapshots).
- [~] **M5.3 Slices, many-pointers, slicing, `.len`, `.ptr`, for over slices.**
  - [x] **M5.3a Many-pointer types, array coercions and borrowed indexing.**
  - [x] **M5.3b Pointer arithmetic and differences.**
  - [x] **M5.3c1 Slice descriptor values, array coercions, indexing, storage and internal ABI.**
  - [x] **M5.3c2 Array/slice for iteration with value and pointer captures.**
  - [x] **M5.3c3 Inferred unsentinelled array-pointer/slice joins and comparison guards.**
  - [x] **M5.3d1 Borrowed slicing expressions and bounds checks** (sentinel assertions landed in M5.4d).
  - [x] **M5.3d2 Known slice dereference and borrowed array addresses.**
  - [x] **M5.3e Writable/addressable slice descriptor fields and volatile word access.**
- [~] **M5.4 String literals, sentinels, `[*:0]const u8`.** Literal byte values landed in M4.5b; general string/memory operations and non-scalar sentinels remain pending.
  - [x] **M5.4a Scalar-sentinel many-pointers**: integer/bool promises, coercions and arithmetic.
  - [x] **M5.4b Scalar-sentinel arrays**: explicit syntax, storage, initialization and coercions.
  - [x] **M5.4c Scalar-sentinel slices**: descriptors, inference, inherited slicing and element bounds.
  - [x] **M5.4d Explicit sentinel assertions**: contextual typing, constant checks and ordered runtime guards (validated in isolated checkout).
- [~] **M5.5 Casting builtins**: `@intCast`, `@truncate`, `@ptrCast`, `@intFromPtr`, `@ptrFromInt`, `@bitCast`, `@as`.
  - [x] **M5.5a Pointer address conversion (`@intFromPtr`).**
  - [x] **M5.5b Integer-to-pointer conversion (`@ptrFromInt`)**: constant/numeric addresses and runtime guards (locally validated).
  - [x] **M5.5c Outer const/volatile casts**: shape/provenance preservation and observable access semantics.
  - [x] **M5.5d Alignment casts**: contextual alignment changes, combined qualifier permissions and guards.
  - [x] **M5.5e Scalar bit casts**: equal-width integer/bool/void representations; native-width runtime and arbitrary-width constants.
  - [x] **M5.5f Constant aggregate bit casts**: logical nested arrays, sentinels and undefined/zero-bit representations.

## Phase 6: Aggregates and errors

- [ ] **M6.1 Structs**: fields, init, field access, methods, default values.
- [ ] **M6.2 Enums**: tag values, `@intFromEnum`, `@enumFromInt`, enum literals.
- [ ] **M6.3 switch** on ints/enums with ranges.
- [ ] **M6.4 Optionals**: `?T`, `orelse`, `.?`, if-payloads, optional pointers.
- [ ] **M6.5 Error sets + error unions**: `try`, `catch`, `errdefer`, error set merging, inferred error sets.
- [ ] **M6.6 Unions**: bare and tagged, switch payload capture.
- [ ] **M6.7 extern/packed struct layout.**

## Phase 7: Comptime (`src/comptime.c`)

- [ ] **M7.1 Big integers for comptime_int.**
- [ ] **M7.2 Comptime interpreter over IR**: const decls, arithmetic, control flow.
- [ ] **M7.3 Types as values**: `type`, `@TypeOf`, returning types from functions.
- [ ] **M7.4 Generic functions**: comptime params, `anytype`, monomorphization + memoization.
- [ ] **M7.5 `inline for` / `inline while` / inline switch prongs.**
- [ ] **M7.6 `@typeInfo` / `@Type` / `@This` / `@hasDecl` / `@field` / `@hasField`.**
- [ ] **M7.7 comptime_float (f64-backed for now; f16/f80/f128 = hard error).**

## Phase 8: Modules and std

- [ ] **M8.1 `@import` of files (file = struct), lazy decl analysis.**
- [ ] **M8.2 Lib dir resolution**: `ZIG_LIB_DIR`, `<exe>/../lib/zig`, `<exe>/../lib`; `@import("std")`.
- [ ] **M8.3 `@import("builtin")` generation** (x86_64-linux target info).
- [ ] **M8.4 `start.zig` entry and `std.os.linux` syscalls** (asm strategy, see DESIGN.md).
- [ ] **M8.5 std hello world** (`std.debug.print`, `std.fs.File`).

## Phase 9: Features needed to bootstrap

- [ ] **M9.1 Threadlocal (QBE `thread`), atomics via runtime helpers.**
- [ ] **M9.2 u128/i128 lowering via helpers.**
- [ ] **M9.3 `@Vector` lowered to scalar loops.**
- [ ] **M9.4 Labeled switch `continue :sw`, `@fieldParentPtr`, `@memcpy`/`@memset`, remaining builtins.**
  - [x] **M9.4a `@sizeOf`/`@alignOf` for supported type layouts.**
  - [x] **M9.4b `@bitSizeOf` for supported bit representations.**
- [ ] **M9.5 Inline asm for x86_64-linux std usage.**

## Phase 10: std tests and bootstrap

- [ ] **M10.1 Selected std unit tests pass (`szc test`).**
- [ ] **M10.2 Build options / build strategy for the Zig compiler**: ASK THE USER first (deferred decision Q6).
- [ ] **M10.3 Compile the Zig compiler (no LLVM) → zig2.**
- [ ] **M10.4 zig2 rebuilds itself.**

---

## Completed milestone log

(newest first; each entry: what, limitations, interactions)

### M5.5f Constant aggregate bit casts

- **What**: equal-logical-width constant casts between nested integer/bool/void
  arrays and scalars, independent of ABI stride. The first element occupies low
  bits; signed fields preserve two's-complement bits. Source scalar sentinels
  contribute their stored bits; destination constant arrays restore their declared
  sentinel, as Zig's packed constant reader does. Whole undefined stays undefined;
  undefined fields in otherwise defined representations choose zero. A defined
  sentinel prevents an all-undefined payload from becoming wholly undefined.
  Zero-bit destinations have their unique value, even for undefined input.
- **Implementation**: bounds-checked PackedBits read/write helpers in comptime.c
  use fixed-width limbs, crossing word boundaries without quadratic concatenation
  or shifts by the host width. Sema recursively validates width and constructs
  interned values. Cumulative packed-buffer capacity is limited to 1048576 words
  (4 MiB of buffers, not a total process-memory bound); the existing dense array
  element budget also applies. Representation bit counts are bounded to uint32_t.
- **Validation**: 5509 source/test/config files match both corrected frozen
  GCC/Clang Werror sanitizer trees. Each passes 1243 units/1040 goldens, 96 ordinary/
  generated plus three scoped native cases, 16 driver checks, all C/ABI/storage/
  alignment suites, 60 volatile sequences and 255 safety cases. GCC full e2e uses
  actual Zig; Clang separately references the new native. Optimized GCC passes
  96 native/255 safety. Fresh actual matrices: 337 aggregate + 499 scalar + 409
  alignment + 326 qualifier + 225 numeric pointer comparisons (1796 total).
  Four 60s fuzz runs: 897170/22359/26140/37082; both compilers replay 20000 mutations/stage
  (64 irgen/120 sema seeds), with deterministic corpus ordering and checkpoints.
- **Regressions/limits**: nine negative and three positive stage goldens, native
  nested/signed/bool/sentinel/wide/undefined/zero-bit cases, 379 added unit checks,
  two seeds and the 32nd CI comparison suite. An independent Python-integer
  model additionally passes 5000 randomized 256-bit overwrites and signed/unsigned
  reads, including zero widths and unaligned boundaries. Reference audits caught defined
  sentinel and zero-bit undefined edge cases before integration; corrected final2
  trees were fully revalidated. Runtime aggregate casts, floats/vectors/enums,
  relocations and physical-memory reinterpretation remain separate work.
  Evidence: /data/aggregate-bitcasts-final2-*; source /data/aggregate-bitcasts-next.

### Standalone fuzz reproducibility follow-up

- Bytewise-sorted corpus replay stabilizes the mutation stream across filesystems.
  Optional current-input checkpoints are written before compiler entry, and hosted
  replay is bounded with failure artifact upload. This does not change fuzz targets
  or reduce the 20000 mutations/stage or scheduled libFuzzer budgets.
- Driver tests cover creation-order independence, deterministic mutations, corpus
  cap, empty/single inputs, and checkpoint success/failure under GCC and Clang.
  Fresh independent Werror ASan/UBSan trees pass 864 units/1028 goldens and full
  standalone replay under both compilers. An earlier hosted GCC job ended with
  runner shutdown/143; rerun remains pending, not a confirmed compiler defect.

### M5.5e Scalar bit casts

- **What**: contextual `@bitCast` preserves equal-width scalar representations
  rather than numeric values. Integer signedness changes wrap the same bits;
  bool/u1/i1 conversions preserve the single bit; void/u0 share zero bits.
  Typed undefined remains undefined (zero-bit results have their unique value).
  Raw comptime_int/undefined sources, pointer casts, invalid destinations, width
  mismatches, missing context and bad argument counts are diagnosed.
- **Lowering**: IR_BIT_CAST carries destination type and uncoerced operand.
  Sema uses existing arbitrary-precision wrap operations for constants, and
  TIR_BIT_CAST reuses scalar copies plus canonical destination normalization.
  No numeric-range or overflow guard is emitted for a representation change.
  Runtime widths above 64 retain the existing explicit backend limitation.
- **Validation**: 5480 source/test/config files match both frozen GCC/Clang
  Werror ASan/UBSan trees. Both pass 864 units/1028 goldens, 95 ordinary/generated
  plus three scoped native cases, 16 driver checks, all C/ABI/storage/alignment
  suites, 60 volatile sequences and 255 safety cases. GCC full e2e uses actual
  Zig; Clang separately compares both new native programs with Zig. Generated
  signed/unsigned/extension checks cover every native width 1..64 (948 checks).
  Fresh actual matrices: 499 scalar bit casts + 409 alignment + 326 qualifier +
  225 numeric pointers (1459 total). Optimized GCC passes 95 native/255 safety.
  Four 60s fuzzers execute 1448722/41200/45360/59209 inputs; both compilers replay
  20000 mutations/stage (63 irgen/119 sema seeds). Every final wrapper exits zero.
- **Tests/limits**: 51 additional unit checks, nine diagnostic/three positive
  stage goldens, native signedness/bool/zero-width/wide-constant fixtures, a
  generated runtime-width fixture, two fuzz seeds and the 31st CI comparison
  suite. Arrays/structs/enums/vectors/floats and physical pointer reinterpretation
  are separate work; no padding/relocation bytes are invented. M5.5 remains
  partial. Evidence: /data/bit-casts-final-* and /data/bit-casts-widths2.log.

### M5.5d Alignment casts

- **What**: contextual `@alignCast` for supported single/many/slice pointers,
  including array-pointer to slice coercion. Adjacent alignment/const/volatile
  casts share one destination and explicit permission flags; duplicate flags are
  diagnosed through parentheses. Child types, sentinels, allowzero and nested
  qualifier promises remain checked. Unlike ordinary coercion, this operation
  cannot turn an array pointer into a many-pointer or drop an array sentinel.
- **Safety/identity**: known numeric misalignment is diagnosed; runtime alignment
  increases use the existing incorrect-alignment panic. Runtime slice-to-slice
  casts skip that guard when empty, but known numeric empty slices retain the
  reference's constant check. Thin/array-to-slice casts have no empty exemption.
  Known symbolic roots retain identity/offset and are trusted as in Zig; casting
  never silently upgrades the backing allocation's alignment. Undefined pointers
  require an allowzero destination and remain whole-value undefined.
- **Validation**: frozen GCC/Clang Werror ASan/UBSan trees match all 5451 development
  source/test/config files. Both pass 813 units/1016 goldens, 93 ordinary/generated
  plus three scoped native cases, 16 driver checks, all C/ABI/storage/alignment
  suites, 60 volatile sequences and 255 safety cases. GCC full e2e uses actual Zig;
  Clang separately checks the new native fixture against Zig. Six foreign-C
  descriptor layouts also match actual Zig. Fresh actual matrices: 409 alignment
  casts + 326 qualifier casts + 225 numeric pointers (960 total). Optimized GCC
  passes 93 native/255 safety cases. Four 60s fuzzers execute
  1265226/32903/34588/60922 inputs. Both compilers replay 20000 mutations/stage
  (62 irgen/118 sema corpus files). Every final wrapper exits zero.
- **Interactions/limits**: IR_ALIGN_CAST carries type/operand/typed flags/source;
  TIR_ALIGN_CHECK carries the pointer and empty-slice exemption, without loading
  pointee memory. Existing pointer retagging and owned slice construction preserve
  descriptors. Added 20 unit checks, 11 negative/three positive stage goldens,
  native/C-storage/volatile/safety fixtures, fuzz seeds and the 30th CI comparison
  suite. General ptrCast/bitCast, optional/vector/C/packed pointers and physical
  comptime reinterpretation remain separate. Evidence: /data/align-casts-final-*
  and /data/align-casts-storage.log. M5.5 remains partial.

### M5.5c follow-up: contiguous qualifier-cast diagnostics

- Verified Zig 0.17.0 AstGen's pointer-cast-chain rules and reproduced an
  acceptance mismatch: repeated `@constCast`/`@volatileCast` flags in a contiguous
  chain must be diagnosed. Parentheses do not break the chain, but `@as` and
  named intermediate values do. Irgen now rejects repeats at the inner offending
  builtin without inspecting token spellings in the parser or changing TIR/ABI.
- Added four negative goldens, ten unit scenarios (including malformed arities),
  positive ir/tir/QBE/native boundary checks, two fuzz seeds, and 116 reference
  comparisons for chains/context boundaries (326 total qualifier comparisons).
- Frozen `/data/cast-chain-final` GCC/Clang Werror ASan/UBSan sources/test/config
  match main. Both pass 793 units/1002 goldens, 92 ordinary/generated + three
  scoped native cases, 16 driver checks, all C/storage/ABI/alignment suites,
  58 volatile sequences and 248 safety cases. GCC full e2e checks actual Zig;
  Clang separately reference-checks the new native program. Optimized GCC passes
  92 native/248 safety cases. Four 60s fuzzers execute
  2168688/54704/68109/74591 inputs; both compilers replay 20000 mutations/stage
  with 61 irgen/117 sema corpus files. Every final wrapper exits zero.
- This is a diagnostic correction, not general `@ptrCast`/`@alignCast` support.
  No new memory interpretation, implicit qualifier permission, or layout rule
  is introduced. Logs use `/data/cast-chain-final-*`; reference comparison output
  is `/data/cast-chain-differential.log`.

### M5.5c Outer pointer-qualifier casts
- **What**: `@constCast`/`@volatileCast` infer the source pointer shape and remove
  only its outer const/volatile qualifier. Nested pointee qualifiers, pointer
  size, alignment, allowzero and sentinel are unchanged. Thin values preserve
  existing symbolic/numeric provenance; slice values preserve pointer/length
  and owned descriptor semantics, consistently retagging the element pointer.
  Existing PTR_CAST and descriptor construction suffice; no backend special
  case or pointee copy is introduced. Combined qualifier casts compose before
  ordinary result-context coercion.
- **Validation**: immutable /data/qualifier-casts-final GCC/Clang Werror
  ASan/UBSan source/test/config match development (5401 checked files). Every
  final wrapper, comparison matrix and fuzz process exits zero. 767 units/995
  goldens, 91 ordinary/generated + three scoped native programs, 16 driver checks,
  every C/ABI/alignment suite, 58 observed volatile sequences and 248 safety cases.
  GCC full e2e runs actual Zig; both builds separately reference-check the new
  native fixture, four C descriptor layouts and volatile Debug/ReleaseFast.
  Fresh actual matrices: 210 qualifier casts + 225 numeric pointers + 178 sentinel
  assertions + 256 sentinel slices + 313 arrays + 209 sentinel pointers + 168
  fields + 87 dereference + 204 slicing + 252 slices + 168 coercions + 240 volatile
  pointers (2510 total). Optimized GCC passes 91 native/248 safety; 949 unchanged
  array/layout checks replay the source-hash-verified prior actual-reference
  oracle (not fresh Zig builds). Four 60s fuzzers execute
  1140332/32597/29994/36325 inputs; both builds replay 20000 mutations/stage
  (60 irgen/116 sema seeds). Logs: /data/qualifier-casts-final-*. CI adds the
  qualifier-cast matrix (29 suites). Integrated after user approval as c1c96de;
  the later contiguous-chain correction is 866504f.
- **Interactions/limits**: IR_CONST_CAST/IR_VOLATILE_CAST are unary inferred-type
  operations. Unit checks assert qualifier isolation, layout, promises and no
  volatile load merely from casting. New observer cases retain volatile through
  constCast and remove the discarded ordinary read after volatileCast. Runtime
  slice casts extract/rebuild descriptor values without traversing elements.
  Explicit casts do not make immutable memory safe to mutate. Optional/vector/
  C-pointer variants, packed pointers, general ptrCast, alignCast, bitCast and
  nested multi-cast inference involving unimplemented operations remain separate.

### M5.5b Integer-to-pointer conversion
- **What**: contextual scalar pointer destinations, usize coercion, rejection of
  invalid zero/misaligned constants and runtime null/alignment checks. Absolute
  VALUE_INT_PTR values intern by pointer type/address, preserve numeric identity
  through qualifier casts, element/field addresses, slicing and globals, and
  never claim compiler-owned contents. `@intFromPtr` folds absolute values.
  Addition wraps numeric usize addresses like actual Zig; underflowing subtraction
  is rejected. Numeric backing reads/writes/snapshots occur only at runtime.
- **Validation**: immutable /data/int-pointer-final GCC/Clang Werror ASan/UBSan
  source/test/config match development (5375 checked files). All final wrappers,
  matrices and fuzz scripts exit zero. 739 units/984 goldens; 90 ordinary/generated
  + three scoped native programs; 16 driver checks; all C/ABI/alignment suites;
  54 volatile sequences; 248 safety cases. GCC full e2e runs actual Zig; both
  builds additionally reference-check the new native fixture, three mmap-backed
  C layouts and volatile Debug/ReleaseFast. Fresh actual matrices pass 225 numeric
  pointers + 178 assertions + 256 sentinel slices + 313 sentinel arrays + 209
  sentinel pointers + 168 fields + 87 dereference + 204 slicing + 252 slices +
  168 pointer coercions + 1035 pointer alignment + 864 allowzero + 240 volatile
  pointer cases (4199 total). Optimized GCC passes 90 native/248 safety; 949
  unchanged array/layout checks replay the source-hash-verified prior actual
  reference oracle, not fresh Zig compilations. Four 60s fuzzers execute
  1589115/40732/38438/46299 inputs; both builds replay 20000 mutations/stage
  (59 irgen/115 sema seeds). Logs: /data/int-pointer-final-*. CI adds the numeric
  pointer matrix (28 suites). Integrated after user approval as 0a22870.
- **Interactions/limits**: IR_PTR_FROM_INT carries destination type/address;
  TIR_PTR_FROM_INT lowers to guards plus a long-word copy. Numeric pointers remain
  distinct from relocation roots. Existing borrowed-layout/zero-bit rules apply.
  Slice destinations cannot infer a length; optional/vector/C-pointer destinations
  and general casts are separate work. Known numeric-backed sentinel views follow
  actual Zig's constant runtime-memory bypass, not a fictitious memory read.
  `-fno-emit-bin` comparisons cover known numeric-slice scalar dereference; trying
  that scalar result in the actual C runtime fixture triggered Zig's backend
  `genSetReg called with a value larger than dst_reg` error. The shared native
  C fixture does not claim that known-bug path as a successful reference run.

### M5.4d Explicit sentinel assertions
- **What**: `a[first..last:S]` obtains S's scalar type from the borrowed base,
  including nested `@intCast`. Known immutable backing is validated at analysis;
  runtime assertions check ordering and physical bounds before reading one end
  element, then compare it to the exact integer/bool sentinel. Mismatch panics
  with `sentinel mismatch`. Open many-pointer assertions must match the existing
  promise; single-item pointer assertions are rejected. Known array views at
  offset zero reconstruct the requested physical extent, not their cast root's
  full extent. One-value zero-bit views retain their existing special handling.
- **Validation**: /data/sentinel-assertions-final immutable GCC/Clang Werror
  ASan/UBSan snapshots match development source/test/config. Every final wrapper,
  matrix and fuzz process exits zero. 726 units/972 goldens; 89 ordinary/generated
  + three scoped native programs, 16 driver checks, every C/ABI/alignment suite,
  54 observed volatile sequences and 243 safety cases. All 17 added panic cases
  pass actual Zig; GCC full e2e also runs Zig. Both builds separately reference
  the native fixture, four C descriptor variants and volatile Debug/ReleaseFast.
  Fresh actual comparisons: 178 assertions + 256 sentinel slices + 313 sentinel
  arrays + 209 sentinel pointers + 168 fields + 87 dereference + 204 slicing +
  252 slices. Optimized GCC passes 89 native/243 safety; 949 unchanged array/layout
  checks replay the source-hash-verified prior actual-reference oracle. Four 60s
  fuzzers execute 1592533/52095/49368/68597 inputs; both builds replay 20000
  mutations/stage (58 irgen/114 sema seeds). Logs use
  /data/sentinel-assertions-final-*. CI adds the assertion matrix (27 suites).
- **Interactions/limits**: IR_SLICE gains sentinel refs/source and
  IR_SLICE_ELEM_TYPE provides contextual metadata without reading the descriptor.
  TIR_SENTINEL_CHECK follows the actual scalar load; a unit test verifies the
  order/bounds/load/assertion sequence and one volatile load. Actual Zig elides
  the assertion (including volatile reads) in ReleaseFast; reference fixtures
  explicitly expect zero assertion reads there, not one. Known mutable global
  views and known-result constant-bound guard elision follow actual Zig behavior;
  tests do not execute out-of-backing cases with nondeterministic memory contents.
  Non-integer/bool sentinels, C-pointer types and general casts remain separate.
  Integration/push is pending the environment-block approval, not test completion.

### M5.4c Scalar-sentinel slices
- **What**: integer/bool sentinel descriptors use the existing owned pointer/usize
  representation. Type interning, coercions, joins, `.ptr`/`.len` word addresses,
  globals, internal calls/returns and inherited slicing preserve exact promises.
  Sentinel indexing includes the physical end; explicit unsentinelled windows
  drop it unless they end at the known original logical length. Runtime descriptors
  use the physical bound; known descriptors with dynamic result lengths follow
  Zig's logical-bound guard. Empty-array const exceptions and zero-bit views are
  checked against the actual 0.17.0 compiler, not inferred from source alone.
- **Validation**: immutable /data/sentinel-slices-final2 GCC/Clang Werror ASan/UBSan
  runs exit zero: 718 units/961 goldens, 88 ordinary/generated + three scoped
  native programs, 16 driver checks, all C/ABI/alignment suites, 52 observed
  volatile sequences and 226 safety cases. GCC full e2e runs actual Zig; both
  builds additionally reference-check the new native fixture, four C storage
  variants and volatile sequences in Debug/ReleaseFast. Fresh actual comparisons:
  256 sentinel slices + 313 sentinel arrays + 209 sentinel pointers + 168 fields +
  87 dereference + 204 slicing + 252 slices. Optimized GCC passes 88 native/226
  safety cases; 949 unchanged array/layout cases replay a source-hash-verified
  prior actual-reference oracle (not 949 fresh Zig builds). Four 60s fuzzers run
  1306173/33229/36889/42163 inputs; both builds replay 20000 mutations per stage
  (57 irgen/113 sema seeds). Logs: /data/sentinel-slices-final2-*. Older final is
  superseded after explicitly sequencing C argument materialization to obtain
  identical GCC/Clang stage output. All final wrapper processes exit zero.
- **Interactions/limits**: no descriptor ABI or codegen representation change.
  `.ptr` carries the sentinel independently of descriptor-storage qualifiers;
  direct known slice dereference produces `[len:s]T` backing views. Zero-bit
  runtime indexing still requires its safety guard. Whole volatile descriptor
  copies, non-integer/bool sentinels and explicit sentinel assertions are not
  completed by this milestone. The reference CI matrix grows from 25 to 26;
  hosted status is asynchronous and is not claimed successful from local tests.

### M0.5c Exact-coverage safety reference partitions
- **What**: four safety jobs select stable case ordinals modulo four. The default
  local harness and `make e2e-safety-test` still run every case. Validate positive
  shard counts and bounded indices before executing any compiler. The aggregate
  differential gate still requires all suites; maximum reference concurrency
  remains four and fail-fast remains disabled. No timeout is retried or ignored.
- **Why**: prior hosted runs time out in unrelated Zig/szc build cases before
  runner shutdown. Those cases pass locally with their original compilers and
  deadlines; shorter independently reported jobs limit the affected work without
  declaring infrastructure failures successful or removing a test.
- **Validation**: 22 partition/CLI checks prove exact multiset coverage and order
  for 1/2/4/7 shards, balanced sizes and invalid argument rejection. YAML checks
  retain every other job, the success gate, pins, process deadlines and fuzz
  budgets; the reference matrix grows from 22 to 25 suites. All four actual
  native/Zig shards pass 55 cases each using the final M5.4b sanitizer compiler
  and unchanged deadlines (220 total, none skipped). Make dry-run comparison
  retains every prior e2e command exactly and adds only the partition proof.
  Evidence: /data/ci-safety-{partition-validation,final-proof}.log and
  /data/ci-safety-shard-{0,1,2,3}.log. Remote results remain asynchronous.
- **Interactions/limits**: no compiler behavior changes; case additions are
  automatically partitioned and the full local entry point remains unchanged.
  Hosted runner shutdown can still fail a shard and must remain visible.

### M5.4b Scalar-sentinel arrays
- **What**: defined comptime integer/bool sentinel array types/literals, canonical
  ValueId identity, physical storage/stride, constants, locals/globals, snapshots,
  internal calls/returns and compatible concatenation. Anonymous in-place
  initialization stores its sentinel only after ordinary initializer effects.
  Contextual anonymous literals inherit their pointer destination's sentinel;
  physically nonempty sentinel arrays cannot be mistaken for empty OPV values.
- **Grounding**: actual Zig 0.17.0 array literal/type analysis, array/pointer
  in-memory coercion, concatenation and element/slicing bounds. Reference probes
  distinguish immutable nested slots from writable pointer-slot invariance and
  preserve upstream maximal zero-bit physical-length wrapping semantics.
- **Coverage**: unit checks, three-stage goldens, seven invalid inputs, native
  integer/bool/empty/zero-bit/initializer-order checks, four aligned C PIE/RELRO
  storage variants, four new safety cases, irgen/sema seeds and a dedicated
  reference CI suite. All previous tests and deadlines are retained.
- **Validation**: /data/sentinel-arrays-final3 GCC/Clang Werror ASan/UBSan
  snapshots match the integrated source/test/config files. Both pass 692 units,
  950 goldens, 87 ordinary/generated + three scoped native programs, 16 driver
  checks, all C/ABI/alignment suites, 48 volatile sequences and 220 safety cases.
  GCC full e2e also runs Zig; both builds separately reference-check the new
  native program, four C sentinel-array variants and volatile Debug/ReleaseFast.
  Actual semantic matrices pass 313 array-sentinel + 209 sentinel-pointer + 168
  fields + 87 dereference + 204 slicing + 252 previous slice cases. Optimized GCC
  passes 87 native/220 safety cases. The 949 unchanged array/layout checks replay
  a source-hash-verified prior actual-reference oracle, not fresh Zig runs.
  Four 60-second fuzzers execute 949201/24630/26297/41516 inputs; both builds
  replay 20000 mutations/stage (56 irgen/112 sema seeds). Every final process
  exits zero; /data/sentinel-arrays-final and final2 are superseded snapshots.
- **Interactions/limits**: TypeArray now stores the actual sentinel, not a flag;
  existing aggregate lowering carries its physical element. Sentinel slices,
  explicit slice sentinel assertions and other sentinel child types remain NYI.

### M5.4a Scalar-sentinel many-pointers
- **What**: `[*:value]T` for defined comptime integer/bool values; type interning,
  naming, thin ABI, literal zero-sentinel coercion, qualification/nested memory
  rules, inference, indexing and arithmetic/open-tail propagation. No runtime
  scan is implied by the promise; bounded windows do not acquire a sentinel.
- **Grounding**: Zig 0.17.0 zirPtrType, checkSentinelType, pointer coerce checks,
  analyzePtrArithmetic and Type.elemPtrType. Promises survive pointer offsets but
  not element-address formation; missing/mismatched promises cannot be coerced
  into a stronger type. Mutable nested pointer slots require invariance.
- **Coverage**: canonical equal-value/distinct-value/type/layout unit checks,
  stage goldens, seven errors and replacement of the old supported-syntax NYI
  fixture. Native tests scan string/empty/embedded-NUL backing and verify global
  slots, arrays of pointers, callbacks, offsets and bounded windows. Existing C
  PIE/RELRO variants now return writable, unaligned nonzero-sentinel pointers.
  A dedicated semantic comparison matrix is part of reference CI; fuzz seeds
  cover both irgen and sema.
- **Validation**: /data/sentinel-many-final immutable GCC/Clang sources match
  1673 checked integrated source/test files. Werror ASan/UBSan builds pass 666
  units/940 goldens, 86 ordinary/generated + three scoped native programs,
  16 driver checks, all C/ABI/alignment suites, 48 volatile sequences and 216
  safety cases. GCC full e2e runs actual Zig; both compilers also separately
  reference-check sentinel_pointers.zig, five C many-pointer variants and all
  volatile sequences. Actual semantic comparisons pass 209 new sentinel cases
  + 168 fields + 87 dereference + 204 slicing + 252 old slice cases. Optimized
  GCC passes 86 native/216 safety checks. The 949 unchanged array/layout cases
  replay the source-hash-verified previously actual-reference-validated oracle,
  not fresh Zig compilations. Four 60-second fuzzers execute
  1487814/47977/51638/67325 inputs; both builds replay 20000 mutations/stage
  (55 irgen/111 sema seeds). All final scripts exit zero; logs share that prefix.
- **Limits/next**: sentinel slices and explicit array/slicing sentinels remain
  NYI, as do non-integer/bool sentinel values. Runtime wide-integer pointees keep
  their existing restrictions; pointer promises do not establish backing bounds.

### M5.3e Writable/addressable slice descriptor fields
- **What**: `.ptr`/`.len` assignment, compound assignment and borrowed addresses
  for local/global/nested descriptors, including pointer-to-slice field access.
  Field words can be initialized independently without loading an undefined
  descriptor. Mutable element pointers do not imply mutable descriptor storage;
  immutable descriptors/parameters retain correct constant-field errors.
- **Grounding**: Sema's slice fieldPtr path, Type.fieldPtrType and upstream
  behavior/slice.zig field constness/alignment/global mutation tests. Actual Zig
  comparison caught an over-alignment mistake: even descriptor word zero must
  cap field-address alignment to the word's ABI alignment, not inherit excess.
- **Interactions**: new IR_FIELD_PTR and VALUE_SLICE_FIELD_PTR preserve selected
  word, original descriptor root and cumulative offset through casts/array paths.
  Constant loads, function queues, hashing, equality, static relocations and
  QBE operands honor field provenance. Existing scalar coercion/store and volatile
  helpers implement individual words; plain field address formation does not
  load descriptor contents or create a mutable snapshot destination.
- **Coverage**: eight interning/provenance unit checks, three stage goldens,
  six new error cases and a positive volatile-field golden. The former volatile
  field NYI fixture now tests an actual whole-descriptor copy instead. Native
  tests cover escaped field addresses, nested/global aliases, once-only compound
  operands, snapshots, unaligned storage and per-word initialization. Four C
  PIE/RELRO variants check actual descriptor-word relocation addresses. The
  volatile observer adds four access/no-access sequences checked against Zig
  in Debug/ReleaseFast; three safety cases verify updated lengths guard indexing
  and slicing. Add fuzz seeds and an independent semantic CI suite.
- **Validation**: immutable /data/slice-fields-final GCC/Clang source/test copies
  match the integrated tree (1659 files checked). Both Werror ASan/UBSan builds
  pass 640 units/930 goldens, 85 ordinary/generated + three scoped native cases,
  16 driver checks, all C/ABI/alignment suites, 48 volatile sequences and 216
  safety cases. GCC's full e2e suite also runs Zig; both builds separately
  reference-check the new native case, four C descriptor variants and all 48
  volatile sequences in Debug/ReleaseFast. Actual semantic comparisons pass
  168 fields + 87 dereference + 204 slicing + 252 previous slice cases. Optimized
  GCC passes 85 native/216 safety checks; 949 unchanged array/layout cases replay
  against the source-hash-verified actual-reference oracle (not fresh Zig runs).
  Four 60-second fuzzers execute 1430533/44280/53821/66966 inputs without failures;
  both builds replay 20000 mutations/stage (54 irgen/110 sema seeds). Logs use
  /data/slice-fields-final-*.
- **Limits/next**: whole-descriptor volatile copies and sentinel descriptors/
  slicing remain NYI; writable fields do not make arbitrary pointer/length pairs
  safe to dereference. M5.3 remains partial. Q6 stays deferred to M10.2.

### M0.5b Independent native reference suites
- **What**: split the remaining monolithic native reference job into native
  programs/harness/driver, C storage/ABI, alignment/volatile, and safety suites.
  `make e2e-test` remains the complete local entry point; four explicit targets
  expose the same commands to CI. The differential matrix now has twenty suites
  (15 semantic tools, parser and four execution suites), max concurrency four.
- **Validation**: Make dry-run comparison retains all 24 original e2e commands
  exactly once in both the umbrella and the union of partitions. All four
  partitioned Clang ASan/UBSan suites run successfully on the final M5.3e sources;
  GCC's complete original reference suite also passed those same commands.
  YAML and routing checks verify all twenty suites, QBE in each execution suite,
  unchanged GCC/Clang/fuzz and aggregate jobs, tool pins, parser corpus/1000
  mutants/run seed, all deadlines and test budgets. Evidence:
  /data/ci-native-coverage.log and /data/ci-native-{native,storage,alignment,safety}.log.
- **Interactions/limits**: no compiler changes and no failed test is bypassed.
  The aggregate `differential` still requires every suite to succeed; runner
  shutdown/timeouts remain failures. Remote results remain asynchronous.

### M0.5a Bounded parallel reference CI
- **What**: replace the long serial differential job with sixteen independent
  suites: fourteen semantic tools, native/reference execution, and parser span/
  mutation checks. Cap reference concurrency at four, disable fail-fast, and
  retain a final `differential` success gate requiring every suite to pass.
- **Why**: earlier runs repeatedly stalled or received runner shutdown during
  reference native/safety checks, preventing later semantic/parser suites from
  running. Separate jobs expose independent results promptly; this is not a
  workaround that declares timeouts successful or weakens the checks.
- **Validation**: YAML parses, matrix names exactly cover all fourteen repository
  semantic differential tools plus native/parser, prior GCC/Clang and fuzz jobs
  are identical, and the aggregate status cannot pass failed/skipped suites.
  Pinned Zig/QBE, full native checks, parser corpus, 1000 mutants/run seed,
  artifact upload and all existing process deadlines/budgets are preserved.
  Local coverage verification is /data/ci-sharding-validation.log. Remote CI
  remains asynchronous; static validation is not a claim that Actions is green.
- **Interactions/limits**: each suite builds its own compiler/reference tools;
  QBE is needed only for native tests and the span dumper only for parser tests.
  Runner-level failures remain possible and must still be inspected, not retried
  away or counted as semantic successes. Compiler implementation is unchanged.

### M5.3d2 Known slice dereference and borrowed array addresses
- **What**: IR_DEREF/REF_DEREF accept known descriptors. `&s.*` preserves borrowed
  backing and pointer qualifiers/alignment; direct loads follow Zig's actual
  constant/runtime fallback rules. Runtime-known descriptors retain the upstream
  indexing-required diagnostic. No new IR, TIR or codegen instruction is needed.
- **Grounding**: Sema.validateDeref, analyzeLoad and maybeDerefSliceAsArray plus
  test/behavior/slice.zig. Direct one-possible-value children fold to an element,
  not an array; immutable backing gives an array, while mutable global backing
  (even empty views) falls back to an element load.
  Explicit `(&s.*).*` copies an array view when backing contents are runtime-known.
- **Coverage**: native constant/type/byte/zero-bit windows, borrowed mutation,
  alignment and private array snapshots; three stage goldens, five errors and
  two fuzz seeds. The semantic matrix tests result types as well as acceptance,
  including rejected strengthening/discarding of pointer attributes.
- **Reference quirks**: official Zig crashes on undefined zero-bit address
  cancellation, and its native backend rejects the nonempty mutable fallback
  with genSetReg's size error. szc safely diagnoses the invalid address. Mutable
  fallback types are checked with actual Zig sema, while native reload behavior
  lives in the explicitly szc-only suite. Neither reference failure counts as a
  passing native comparison; see docs/IR.md and the dedicated test comments.
- **Validation**: corrected immutable /data/slice-deref-guard-final GCC/Clang
  Werror ASan/UBSan builds pass 632 unit checks/920 goldens, full 84 ordinary/
  generated + three explicitly scoped native programs, 16 driver checks, all
  C/alignment/ABI/volatile suites and 213 safety cases. Both compilers execute
  the two slice programs and four C descriptor variants against actual Zig.
  All 87 dereference + 204 slicing + 252 existing slice semantic cases match
  actual Zig. Optimized GCC passes 84 ordinary + three scoped native programs
  and 213 safety cases; 949 unchanged array/layout cases pass source-hash-verified
  replay of the previously reference-validated oracle (not fresh Zig executions).
  Fresh four-stage 60-second fuzzers execute 1825141/56471/76535/88773 inputs;
  both compiler builds replay 20000 mutations/stage (irgen 53/sema 109 seeds).
  Integrated compiler sources match both corrected immutable snapshots exactly.
  Older /data/slice-deref-final runs predate the empty-mutable fallback correction
  and are superseded, not final evidence.
- **Limits/next**: sentinel descriptors/slicing, writable descriptor fields,
  volatile whole-descriptor copies and inline/comptime iteration remain pending.
  The dense one-value array constructor respects existing count/budget limits.
  M5.3 stays partial and Q6 remains deferred to M10.2.

### M5.3d1 Borrowed slicing expressions and bounds checks
- **What**: array, array-pointer, descriptor, many-pointer and bounded single-item
  slicing. Known bounds produce array pointers, dynamic bounds produce two-word
  descriptors; open many-pointer tails stay many pointers. Preserve qualifiers,
  reduced alignment, borrowed backing, nested views and known zero sentinels.
- **Grounding**: Zig 0.17.0 Sema.analyzeSlice (31423 onward) and simple_panic.
  Evaluate the base address then bounds, then load the descriptor; bound calls can
  replace the descriptor before it is read. Runtime repeated `n..n` stays a slice,
  not an inferred zero-array pointer. The actual-reference matrix caught and
  corrected this inference mistake before completion.
- **Interactions**: IR_SLICE stores base-address/start/end references and source
  locations. TIR start/end guards enforce <= (not indexing's <), in source order.
  Constant window dereference and direct element reads retain root/offset identity,
  including byte views and nested logical arrays. Existing descriptor snapshots,
  private result transport, pointer offsets and aggregate copies are reused.
- **Coverage**: three stage goldens, eight error/NYI fixtures, native aliasing,
  nested/type/byte windows, once-only evaluation and descriptor replacement.
  C PIE/RELRO checks cover static view relocations and dynamic descriptor windows
  at four alignments. Twelve new safety cases include empty windows, reversed
  bounds, end overflow and zero-bit children. Each reversed-bound case checks
  Zig's numeric FullPanic text independently of szc's minimal generic message.
- **Validation**: immutable /data/slicing-final GCC/Clang Werror ASan/UBSan
  builds pass 632 unit checks/912 goldens each, full 83 ordinary/generated + two
  scoped native programs, 16 driver checks, all C/alignment/ABI/volatile suites
  and 213 safety cases. The GCC full suite also executes reference Zig; Clang
  independently reference-checks the new native program and four C view variants.
  All 204 new slicing and 252 existing slice semantic cases match actual Zig.
  Optimized GCC passes 83 native programs/213 safety cases. All 949 unchanged
  array/layout cases pass source-hash-verified replay of the previously
  actual-reference-validated baseline, not a fresh Zig execution. Four fresh
  60-second fuzzers execute 2330629/41847/39211/36390 inputs; both compiler
  builds replay 20000 mutations/stage (irgen 52/sema 108 seeds). Integrated
  compiler sources match both frozen snapshots byte-for-byte.
- **Limits/next**: explicit sentinel slicing and runtime sentinel descriptors,
  descriptor-field assignment, volatile whole-descriptor copies and inline/comptime
  iteration remain NYI. M5.3 remains partial. Q6 is still deferred to M10.2.

### M5.3c3 Inferred unsentinelled slice joins and aggregate boundaries
- **What**: differently sized single-pointer arrays infer a slice in an untyped
  runtime branch/block join when their element representations are compatible.
  Merge const/volatile/allowzero qualifiers and minimum alignment, preserve both
  lengths and original borrowed backing, and support empty/nested-pointer cases.
  Typed contexts continue to coerce before joining rather than infer a new type.
- **Grounding**: actual Sema.zig resolvePeerTypesInner `.ptr` path combines
  attributes, first attempts in-memory array compatibility, then produces a slice
  when array lengths differ and children are compatible. Common sentinel slices
  are intentionally NYI, not silently changed into unsentinelled types; an
  explicit []const u8 context (including panic) can still discard a sentinel.
- **Correctness audit**: reject equality of slices after peer resolution, including
  a slice inferred from two different array-pointer operands. Prior constant slice
  equality could incorrectly enter the scalar pointer-root comparison path. Actual
  Sema.cmp and Type.isSelfComparable reject slice comparisons even when known.
- **Coverage**: a native program checks both branch lengths, borrowed mutation,
  value-producing blocks, empty input and sentinel/non-sentinel mixed inputs.
  Add three stage goldens, two invalid-input and one explicit sentinel-NYI
  diagnostic, plus constant/runtime equality diagnostics and fuzz seeds. Fifty-six
  inferred-join and sixteen comparison cases extend actual-reference slice tests
  from 180 to 252. Generic pointer equality remains supported.
- **Validation**: post-fix immutable GCC/Clang Werror ASan/UBSan snapshots
  pass 632 unit checks and 901 goldens each. Integrated source and the complete
  252-case actual-Zig semantic matrix match the tested source byte-for-byte.
  Full native suite passes 82 ordinary/generated + two scoped programs, 16 driver
  checks, all C/alignment/ABI/volatile suites and 201 safety cases. Three targeted
  programs also execute against Zig; Clang passes those native programs plus four
  C descriptor variants, and optimized GCC passes three programs and 201 safety
  cases. The unchanged 949 array/layout cases pass source-hash-verified replay
  against the actual-reference-validated baseline (not a fresh Zig run).
  Fresh four-stage 60-second fuzz runs execute 726928/15857/15392/17505 inputs;
  standalone replay passes 20000 mutations/stage (irgen 51/sema 107 seeds).
  Pre-fix /data/slice-join-final runs were superseded and are not final evidence.
- **Limits/next**: slicing syntax, sentinel slices, descriptor-field assignment,
  volatile whole-descriptor copies and inline/comptime sequence iteration remain
  NYI. M5.3 stays partial. Q6 remains deferred to M10.2.

### M4.5c Dynamic byte-slice panic messages
- **What**: `@panic` accepts byte-slice values with runtime lengths, including
  parameters, mutable descriptors/globals, mutable byte arrays, cleanup operands
  and branches with differently sized array/string inputs. The builtin supplies
  a `[]const u8` result type to its argument; each branch coerces before joining.
- **Grounding**: actual Sema.zig zirPanic coerces to slice_const_u8, resolves a
  constant string only during comptime execution and passes the descriptor at
  runtime. The szc stub already takes pointer/length, so no sentinel scan or new
  backend ABI is needed. Volatile/allowzero byte slices cannot discard qualifiers.
- **Comptime/interactions**: resolve immutable bytes through qualified many-pointer
  indexing and existing provenance-aware loads. This handles byte-array-backed
  slices as well as literals, preserving embedded NUL/non-UTF8 bytes in diagnostics.
  No aggregate reaches the scalar materializer; runtime lowering extracts both
  descriptor words and uses the existing TIR_PANIC and abort/message stub.
- **Coverage**: eight byte-exact runtime/reference cases cover dynamic parameters,
  binary bytes, empty messages, globals, short/long joins and deferred panic. Add
  16 actual-reference semantic cases, three stage goldens, four diagnostics and
  fuzz seeds. The former different-length panic-join NYI fixture is now positive.
- **Validation**: immutable GCC/Clang Werror ASan/UBSan pass 632 units/893
  goldens each, and 180 cases match actual Zig. Full GCC native/reference suite
  passes 81 ordinary/generated + two scoped programs, 16 driver checks, all C/
  alignment/ABI/volatile suites and 201 safety cases. Clang passes all 201 safety
  cases against Zig plus three targeted executables; optimized GCC also passes
  201 safety cases. A mistyped exploratory native-test filename was corrected
  and the three-program command rerun successfully. The unchanged 949 array/
  layout inputs match a source-hash-checked, actual-reference-validated compiler
  baseline replay; these are not claimed as 949 fresh Zig executions here.
  Fresh four-stage 60-second fuzzing executes 426616/9122/9407/10106 inputs;
  20000 mutations/stage pass (irgen 49/sema 105 seeds).
- **Limits**: undefined runtime messages remain explicit NYI. Comptime message
  materialization is capped at 16777216 bytes; the general interpreter and string
  operations are still pending. Panic remains the minimal libc-backed runtime.

### M5.3c2 Array and slice for iteration
- **What**: runtime `for` accepts array values, pointers to arrays and slices;
  supports value/discard/pointer captures, multiple sequence/range inputs,
  unbounded counters, labeled exits, break values, continue, else and scope cleanup.
  Sequence expressions are evaluated once. Reassigning a slice variable in the
  body does not change the captured descriptor or iteration length.
- **Grounding**: actual Zig AstGen.forExpr evaluates sequence inputs as values.
  An array-value operand is therefore an owned snapshot, while a pointer to an
  array borrows its live storage. A slice snapshots only its descriptor and keeps
  borrowing its elements. Pointer captures of array values reject, as do mutation
  through const element pointers and pointer-modified discards.
- **Interactions**: sequence `.len` enters the existing FOR_LEN checked-length
  path; mixed bounded inputs must have equal lengths before the first iteration,
  including empty inputs. A semantic-only FOR_PTR_CHECK rejects array-value
  reference captures. Captures reuse qualified checked indexing and aggregate
  snapshots; loop/defer/labels reuse existing control targets, not a second engine.
- **Coverage**: native tests distinguish array snapshots from live pointer reads,
  descriptor reassignment, one-time function operands, nested arrays/slices,
  mutable pointer captures, labeled cleanup and empty else. 51 new reference cases
  extend the slice matrix from 113 to 164; six invalid-input diagnostics,
  three stage goldens, the formerly NYI array-loop fixture now positive, three
  runtime mismatch cases and two fuzz seeds cover the lowering boundary.
- **Validation**: frozen GCC/Clang Werror ASan/UBSan pass 632 units and 886
  goldens each; integrated compiler sources match those snapshots exactly. Fresh
  actual-Zig comparisons pass all 949 array/layout and 164 slice/iteration cases.
  The full native/reference suite passes 81 ordinary/generated + two scoped
  programs, 16 driver checks, all C/alignment/ABI/volatile suites and 193 safety
  cases. Clang and optimized GCC pass three targeted programs and four C slice
  variants. Fresh four-stage 60-second fuzz runs execute 613035/12984/12499/15214
  inputs; 20000 standalone mutations/stage also pass (irgen 48/sema 104 seeds).
- **Limits/next**: general slicing syntax, sentinels and inline/comptime iteration
  remain NYI. Unbounded many-pointers have no length and cannot be loop inputs.
  M5.3 stays partial; Q6 remains deferred to M10.2.

### M5.3c1 Owned slice descriptors and borrowed elements
- **What**: unsentinelled slice types and qualifiers, array-pointer/string-to-slice
  coercions, `.ptr`/`.len`, scalar/nested indexing and element addresses/mutation.
  Descriptor copies preserve the pointer/length pair without copying elements.
  Support local variables/constants, mutable/immutable globals, arrays of slices,
  pointer-to-descriptor field reads, internal parameters/returns and value joins.
- **Representation**: VALUE_SLICE interns separate pointer/usize values. Runtime
  slices use the existing owned aggregate-snapshot discipline, never scalar QBE
  temporaries. Storage is 16 bytes, alignment 8, with real relocations/dependencies;
  internal calls use transport pointers and a trailing return destination just like
  arrays. C ABI slice parameters/results reject; pointers to descriptors are normal
  C pointer arguments. New TIR operations address the length field and guard dynamic
  indices; existing many-pointer indexing preserves actual backing/qualifiers.
- **Grounding**: actual Sema.zig coerceArrayPtrToSlice/checkPtrAttributes,
  coerceInMemoryAllowedPtrs and slice indexing paths, Type slice layout and official
  execution/semantic probes. Empty array conversions may discard const/strengthen
  alignment; array-pointer conversion follows Zig's distinct allowzero path rather
  than incorrectly reusing the ordinary in-memory pointer rule. Undefined array
  pointers still give a known descriptor length. Slice qualification changes reuse
  pair storage without losing either word. Mutable descriptors remain runtime loads.
- **Coverage**: a native integration program exercises copies, functions, joins,
  globals, arrays of descriptors and mutable pointer-to-slice field reads. Add three
  stage goldens, eight invalid-input plus two explicit NYI diagnostics, seven value
  interning checks, 113 actual-reference semantic cases, five runtime safety cases
  and seeds. Four C/PIE variants inspect/update 16-byte descriptors via memcpy at
  alignments 1/2/8/256 and verify parameter snapshots survive foreign writes.
- **Validation**: immutable GCC/Clang Werror ASan/UBSan snapshots pass 632 unit
  checks and 877 goldens each. All 949 existing array/layout cases plus 113 slice
  cases match actual Zig 0.17.0. Full native/reference suite passes 80 ordinary/
  generated programs, two scoped szc-only cases, 16 driver checks, all storage/
  alignment/ABI/volatile tests and 190 safety cases. Clang and optimized GCC also
  pass three targeted programs and four C descriptor variants. Fresh four-stage
  60-second fuzz runs execute 691770/14954/14604/16711 inputs; standalone replay
  passes 20000 mutations/stage (irgen 47/sema 103 seed files).
- **Limits/next**: `s[start..end]` syntax, sequence iteration, sentinel slices, direct
  descriptor-field assignments and volatile whole-descriptor copies remain NYI.
  Runtime addresses of comptime-only backing/wide values retain existing limits.
  Index/address checks apply to zero-bit children; the reference may fold some
  one-possible-value reads before runtime safety, so reference panic comparisons
  use address formation for those children. M5.3 stays partial; Q6 stays M10.2.

### M9.4b Representation bit-size queries
- **What**: `@bitSizeOf(T)` folds to a comptime integer for integers, bool, void,
  f32/f64, single/many pointers and arrays of representable children. Arrays
  multiply representation bits, not padded ABI bytes: [3]u257 has 771 bits but
  occupies 144 bytes. Bool has one bit despite occupying one byte.
- **Grounding/interactions**: actual Sema.zig zirBitSizeOf and Type.bitSize /
  hasBitRepresentation. Unlike size queries, even empty arrays of comptime-only
  children have no bit representation and reject. Pointers use 64 bits independent
  of their child's representation. Any zero-length array dimension collapses a
  representable nested array to zero before multiplication, avoiding false overflow.
  The new IR operation folds in sema; no TIR/codegen representation changes.
- **Coverage**: 52 new actual-reference semantic cases assert sizes across signed/
  unsigned widths, padded/nested/empty arrays, pointers, floating types and rejected
  logical types. Add a native padded-vs-representation program, three stage goldens,
  seven invalid-input diagnostics and one explicit overflow-NYI fixture, plus one
  seed per semantic stage. Numeric computation checks uint64 multiplication rather
  than wrapping or crashing on oversized representation counts.
- **Validation**: GCC/Clang Werror ASan/UBSan: 625 units and 864 goldens;
  integrated sources exactly match the isolated tested snapshot. Complete GCC
  native and reference-enabled executable suites pass: 79 ordinary/generated
  programs plus two scoped szc-only cases, 16 driver checks, all storage/alignment/
  ABI/volatile tests and 185 safety cases. Clang passes four targeted programs
  and four C-backed stride variants against Zig; optimized GCC also passes these
  native checks. All 949 semantic cases match official Zig 0.17.0. Four fresh
  60-second fuzz runs execute 1131133/28941/28049/32071 inputs; each stage also
  passes 20000 mutations (irgen 46/sema 102 seed files).
- **Limits/next**: bit sizes for error sets, packed aggregates and vectors
  depend on their future implementations; slices have no bit representation and reject. f16/f80/f128 remain hard errors; array
  bit sizes exceeding uint64 are explicit NYI. M9.4 remains partial. General runtime
  slice pairs/slicing are the next larger representation milestone; Q6 stays M10.2.

### M9.4a Type layout queries and exact wide-integer ABI strides
- **What**: `@sizeOf(T)` and `@alignOf(T)` produce comptime integers for supported
  integer, float, bool, void, pointer and array layouts. Runtime values are not
  needed. Size rejects uninstantiable and comptime-only types, while alignment
  of comptime-only types is available. Empty logical arrays retain their zero-byte
  one-possible-value representation; zero-length arrays still inherit child alignment.
- **Correctness fix**: integer ABI bytes round up to their alignment, not to the
  next power of two once alignment caps at 16 on x86_64-linux. In particular u257
  occupies 48 bytes, u513 occupies 80, and u65535 occupies 8192. This also fixes
  many-pointer offsets, differences and nested-array strides for these types.
  No wide-integer runtime arithmetic/load support is claimed.
- **Grounding**: actual Sema.zig zirSizeOf/zirAlignOf, Type.abiSize/abiAlignment and
  std.zig.target intByteSize/intAlignment, plus official binary probes. Query IR
  folds entirely in sema; existing codegen/address operations use corrected layout.
- **Coverage**: 85 new actual-reference semantic comparisons assert numeric sizes
  and alignments (signed/unsigned widths through 65535, nested/empty/zero-bit arrays,
  functions/comptime types, pointers/qualifiers and rejection cases). Four C/PIE
  executions check runtime wide-pointer strides and differences against real storage
  without loading wide values. Add stage goldens, nine diagnostic fixtures, 13 unit
  checks and one seed per semantic stage; wire C checks into normal CI.
- **Validation**: GCC/Clang Werror ASan/UBSan: 625 unit checks and 853 goldens.
  Full GCC native suite: 78 ordinary/generated programs plus two scoped cases,
  16 driver checks, all storage/alignment/ABI/volatile checks and 185 safety cases.
  Clang/optimized builds pass targeted execution; Clang runs three programs and
  four C variants against the actual reference. All 897 semantic comparisons match
  Zig 0.17.0. Four 60-second fuzz runs execute 1374096/33330/34340/38554 inputs,
  plus 20000 mutations per stage (irgen 45/sema 101 seed files).
- **Limits/next**: aggregate/error-set layouts, vector representations and
  overflowing array sizes stay explicit NYI. f16/f80/f128 remain hard errors.
  M9.4 remains partial; general runtime slices and other builtins remain future work.

### M5.5a Pointer address conversion (`@intFromPtr`)
- **What**: expose usize addresses of supported single/many pointers, including
  variables, global/constant/byte backing, elements/offsets, zero-bit storage and
  function symbols. Defined symbolic addresses require runtime; namespace/comptime
  attempts reject instead of inventing integer addresses. Known undefined pointers
  propagate undefined usize, including normal result coercion. Nonpointer/bare
  function operands, wrong arity and implicit runtime narrowing reject.
- **Representation/interactions**: a direct IR_INT_FROM_PTR builtin lowers to the
  PTR_TO_INT operation already used by differences. Codegen copies the real address;
  existing constant/root dependency emission and function reachability remain in
  charge of backing and symbols. No numeric-pointer representation or fake address
  interning is added, and pointer qualifiers do not change address bits.
- **Grounding/coverage**: actual Sema.zig zirIntFromPtr/Type.isPtrAtRuntime plus real
  source/emission probes. Add a native address/alias/offset/alignment/zero-bit/function
  program; IR/TIR/QBE goldens, five reference-rejected errors and an explicit
  comptime-only-backing NYI fixture; one seed per semantic stage. Four C/PIE variants
  compare numeric data/element/function addresses against uintptr_t, invoke the real
  exported callback and check absence of TEXTREL at storage alignments 1/2/16/256.
  Use supported export/extern conventions rather than depending on unimplemented
  explicit callconv syntax. The small standalone-IL program uses function align(16);
  full-assembly C variants additionally verify function align(64).
- **Validation**: GCC/Clang Werror ASan/UBSan: 612 unit checks and 841 goldens each.
  GCC full native suite: 77 ordinary/generated programs plus two scoped cases,
  16 driver checks and all storage/alignment/ABI/volatile/185 safety checks.
  Targeted reference execution and four C/PIE variants pass, including foreign null
  allowzero addresses; Clang and optimized builds pass targeted native/C checks.
  All 812 semantic cases match actual Zig 0.17.0. Four fresh 60-second fuzz runs
  execute 1301767/32659/30619/36404 inputs, plus 20000 mutations per stage.
- **Limits/next**: vectors/optionals and runtime addresses of comptime-only
  backing remain explicit NYI with the rest of those representations; known undefined
  values still propagate before that backing limitation. Raw integer pointer creation
  and other casting builtins remain future work. M5.5 and M5.3 remain partial; general
  runtime slice pairs subsequently land in M5.3c1; slicing remains next. Q6 stays at M10.2.

### M5.3b Pointer arithmetic, differences and compound assignments
- **What**: many-pointer plus/minus usize offsets, `+=`/`-=`, runtime/known pointer
  differences (including upstream's single-array-pointer element-unit rules), actual
  root-relative initializer relocations and reads, qualification/alignment weakening,
  undefined propagation and defined zero-size no-op arithmetic. Negative integer
  offsets, known root underflow, negative known differences and zero-size differences
  reject. Reverse integer-plus-pointer and invalid pointer operators stay rejected.
- **Representation/interactions**: PTR_ADD/PTR_SUB emit scaled address arithmetic with
  no invented length/overflow guard. PTR_TO_INT exposes internal pointer addresses;
  subtraction uses wrapping byte differences and unsigned element division, not signed
  C ptrdiff semantics. Known root identities are shared with pointer equality.
  ADD_SUB_RHS_TYPE gives compound many-pointer offsets usize context while retaining
  integer result types and cast inference; new IR goldens cover that distinction.
  Comptime-only element additions retain logical offsets. Actual upstream subtraction
  uses ABI bytes (zero here), so known logical subtraction preserves its address.
- **Grounding/coverage**: actual Sema.zig analyzeArithmetic/analyzePtrArithmetic/
  ptrSubtract and Value.ptrElem, plus executable and semantic probes. Add a native
  arithmetic program, seven errors, TIR/QBE goldens, 24 generated width roundtrips,
  arithmetic qualifier matrices, compound/cast regressions, foreign unaligned pointer
  arithmetic, relocations and reversed unsigned runtime differences. Add one seed to
  each semantic stage.
- **Safety distinction**: szc reuses its exact-division debug check for non-integral
  pointer differences. Upstream raw AIR div_exact does not promise that panic; such
  execution is illegal behavior. The fifth foreign test checks only szc's diagnostic,
  not reference execution on an invalid input. Four valid variants still execute both.
  An initial harness incorrectly demanded a reference panic, was corrected after
  reproducing official exit 0, and the corrected suites were rerun. No mismatch was
  hidden and no compiler failure was inferred from that wrong test assumption.
- **Validation**: final compound-aware GCC/Clang -Werror ASan/UBSan pass 612 unit
  checks and 832 goldens. Full reference-enabled GCC e2e passes 76 ordinary/generated
  inputs, two scoped inputs, 16 driver checks, all storage/alignment/ABI/volatile suites
  and 185 safety cases. Clang passes four native/reference inputs, all five foreign
  variants and 185 native safety cases; optimized GCC passes four native inputs and
  five foreign variants. The complete final semantic matrix matches all 780 real Zig
  cases, including 36 arithmetic alignment/allowzero and four compound cases. Four
  fresh 60-second fuzz runs pass (973812/23596/21277/26842 inputs), plus 20000 standalone
  mutations per stage (43 irgen/99 sema retained seeds).
- **Limits/next**: ordinary pointer arithmetic does not create bounded pointers or
  slices. Slice pairs, slicing/fields and for integration remain future milestones.
  Layout/known offset overflow remains an explicit NYI; logical runtime arithmetic
  and opaque/unsupported layouts reject. Enclosing M5.3 stays partial; Q6 at M10.2.

### M5.3a Many-pointer types, coercions and borrowed indexing
- **What**: `[*]T` types with const/volatile/allowzero/alignment qualification;
  array-pointer coercions and qualifier-preserving joins; runtime reads/writes,
  nested array borrowing/snapshots, callback/pointer tables, mutable pointer slots,
  and known element addresses/loads backed by their actual original declaration.
  Explicit and anonymous array temporaries infer the correct whole-array backing.
- **Representation/interactions**: IR_POINTER_MANY selects the existing pointer size;
  REF_ARRAY_TYPE recovers anonymous literal length from a many-pointer result context.
  PTR_ELEM_PTR lowers to scaled address arithmetic without an invented length guard.
  Element pointers retain root-relative cumulative offsets; known dereferences walk
  actual backing, rather than manufacturing copied scalar objects. Mutable globals
  still load runtime contents even through const many-pointer views. Existing scalar
  ABI, volatile outlined accesses, unaligned helpers, static relocations, alignment
  weakening and internal owned array snapshots are reused without a second ABI.
- **Grounding/coverage**: actual Zig 0.17 Sema.zig elemPtrOneLayerOnly, array-pointer
  coercion and peer rules, plus real executable probes. Unknown-length addresses may
  exceed the source array length, but known nonzero-size dereferences must fit the
  containing backing. Undefined many-pointer indexing rejects even for zero-bit
  elements; zero-bit defined pointers retain upstream OPV behavior. Add two native
  programs, TIR/QBE goldens, seven errors, anonymous literal cases, 24 integer-width
  read/write roundtrips and four PIE/C/foreign-storage variants, including unaligned
  and volatile mutation, nested relocation offsets and indexes across row boundaries.
  Retain an explicit sentinel-many-pointer NYI fixture; add two seeds per semantic stage.
- **Validation**: GCC/Clang -Werror ASan/UBSan pass 612 unit checks and stage
  goldens (823 final GCC, 822 Clang before the extra IR-only fixture). Full GCC
  reference-enabled e2e passes 75 ordinary/generated programs, two scoped inputs,
  16 driver checks, all storage/alignment/ABI/volatile suites and 185 safety cases.
  Targeted Clang native/reference programs, four foreign variants and all 185 safety
  cases pass; optimized GCC passes three native inputs and four foreign variants.
  All 665 array/many-pointer semantic cases match actual Zig (123 added). Four fresh
  60-second fuzz runs pass (763858/18472/18014/21037 lexer/parser/irgen/sema inputs),
  plus 20000 standalone mutations per stage (42 irgen/98 sema retained seeds).
- **Limits/next**: pointer arithmetic/differences are the next independent checkpoint.
  Slices, slice values/slicing/fields, sentinel pointer syntax, C pointers and array/
  slice for iteration are not implemented here. Unsupported many-pointer dereference
  rejects instead of treating it as a single pointer. Enclosing M5.3 remains partial;
  future slice pairs must not masquerade as one QBE scalar. Q6 stays at M10.2.

### CI follow-up: bounded native/reference compilation and case progress
- **Trigger**: prior 53efbd5 run 38737351381 passed GCC/Clang/fuzz jobs, then its
  differential runner lost communication during native/reference tests. GitHub's check
  annotation identifies a hosted-runner interruption, not a reported language mismatch.
  The newer 1db8afa differential job progressed beyond native tests. Do not attribute
  the earlier host loss to a proven compiler bug or claim it fully solved.
- **What**: e2e compile/emission commands now use the existing process-group cleanup
  helper through a shell CLI adapter with a fixed 120-second compilation budget.
  Reference Zig uses -j2 consistently with the semantic/safety harnesses. Each source
  is logged before work; hangs retain the active source. Runtime execution's existing
  ten-second guard and byte-exact/nonzero exit checks are unchanged; no retries/skips
  or widened acceptance budgets are added.
- **Tests**: CLI bytes/status/signal/missing-command/invalid-budget tests and real
  descendant/pipe cleanup (13); four e2e adapter regressions with spaced paths, reference
  -j2 wiring and compile failures including the expected program status. Compilation
  success remains separate from expected program exit. Existing lifecycle/runner tests
  remain passing (8/18). The hardened harness passes all 73 ordinary/generated
  native+Zig programs, two scoped inputs and 16 driver checks; no compiler source or
  corpus changes, so the preceding full sanitizer/fuzz/language validation still applies.
- **Limits**: this bounds/diagnoses the previously unbounded e2e compile path and reduces
  reference parallelism; it cannot guarantee hosted-runner networking or availability.
  No compiler language milestone advances here. Continue M5.3 after monitoring new CI.

### M5.2c4 Mutable array globals
- **What**: internal mutable typed/inferred and nested array globals, checked layout,
  lazy known initialization, persistent scalar/pointer/callback element mutation,
  whole-array assignment and owned snapshots. Known global element addresses support
  aliases, forward declarations and legal address-only initializer dependencies.
- **Interactions**: global array validation reuses supported aggregate leaf/layout
  rules. VALUE_GLOBAL remains a known address; mutable_global_pointer strips element/
  qualifier paths and checks the actual declaration, never pointer constness. Reads
  through const views of mutable storage remain runtime loads; comptime reads reject
  except existing one-possible u0/void behavior. Known indices create c3b provenance
  pointers; dynamic nested accesses borrow live global backing through index effects.
  Array loads and call/return/cleanup values retain c2/c3a owned snapshots.
- **Length fields**: IR_FIELD_REF supports type-only `.len` through binding/index/deref
  addresses, without loading mutable arrays or array-pointer variables. Ordinary
  rvalue/call bases still evaluate their effects. Undefined dereferences and index
  bounds stay validated; taking length is not permission to dereference undefined.
  Existing typed constant-pointer length goldens now publish harmless backing globals.
- **Storage**: existing global emission creates writable .data/zero storage and writable
  root-plus-offset pointer relocations, including outlined over-aligned arrays. Shared
  static layout/16 MiB budgets remain checked; zero-size objects retain address backing.
  Pointer/callback dependencies are resolved by the existing lazy global/function queue.
- **Tests**: three native/reference global array programs, 24-width snapshot/mutation/
  assignment roundtrips, TIR/SSA goldens, ten initialization/comptime/const/layout/flag/
  volatile error cases, four bounds safety cases and three fuzz seeds. A C/PIE/ELF
  harness checks four alignments, root-relative element addresses, foreign element/
  pointer-table writes and const-alias visibility versus independent array snapshots.
  Fifty-five new array matrix cases compare mutable/global/type-only behavior with Zig.
- **Validation**: frozen GCC -Werror ASan/UBSan/reference suite passes 612 units,
  813 goldens, 73 ordinary/generated programs, 185 safety cases and all existing
  lifecycle/driver/storage/ABI/alignment/volatile suites, plus four C/ELF global-array
  cases. All 542 array comparisons match actual Zig. Clang sanitizer units/goldens/
  15 array+global programs/C storage/185 safety cases and optimized GCC global-array
  checks pass. Four 60s fuzzers and 20k mutations per stage pass; no sanitizer finding.
  Retained irgen/sema corpus counts are 40/96.
- **Limits**: exported/extern/threadlocal globals remain NYI, as do volatile whole-array
  copies, unsupported aggregate leaves, explicit sentinel syntax and broader array-child
  coercions. Undefined initialized storage has no stable language-level contents.
  Enclosing M5.2 remains partial. Next is M5.3 many-pointers and slices; preserve this
  global-address/runtime-contents split when adding array-to-pointer conversions.

### M5.2c3b Comptime element-pointer provenance
- **What**: known array element addresses retain their real root and selection path;
  nested row aliases, pointer dereferences, comparisons and namespace/comptime loads
  no longer fall back to runtime evaluation or references to copied scalar elements.
  Known byte/sentinel addresses, pointer/callback leaves, zero-bit/void and type-only
  arrays share the provenance model. Source writes through immutable elements reject.
- **Interactions**: VALUE_ELEMENT_PTR records an interned parent pointer, index and
  checked cumulative offset. Runtime-layout children use bytes; comptime-only children
  use logical leaf offsets, so equal type values in different slots still have distinct
  element addresses. Loads follow the selection path, not a guessed byte-to-value map.
  Pointer comparisons strip qualifiers and compare root identity plus offset; u0/void
  element pointers share a zero byte offset, matching actual Zig. VALUE_PTR_CAST keeps
  byte backing intact when dropping a string sentinel or adding pointer qualifiers;
  previously retagging VALUE_BYTES could duplicate physical backing and disagree with
  comptime equality. Known byte indexing remains compact rather than copying the string.
- **Lowering**: nonzero element constants materialize root-plus-offset QBE adds. Static
  pointer leaves use root-plus-addend relocations in .data.rel.ro, including outlined
  assembly for >64-byte alignment. Dependency and callback reachability follow the
  root, so no separate copied-element data is emitted. Physical/logical stride and
  cumulative-offset overflow diagnose before interning a wrapped address.
- **Qualifiers**: one shared element-pointer type helper follows upstream elemPtrType:
  preserve const/volatile, retain stronger alignment for index zero or suitably aligned
  constant offsets, use the stride guarantee for dynamic indices, and drop allowzero
  for known nonzero indices. Weakly aligned roots do not gain ABI alignment.
- **Tests**: three native/reference provenance/alignment/outlined-data programs; 24-width
  known-address/dereference roundtrips; TIR/SSA goldens; eight const/bounds/sentinel/
  alignment/undefined/stride/offset errors; root-relative physical relocation and RELRO
  checks; value interning/hash/path unit coverage and three fuzz seeds. The array matrix
  adds 105 acceptance/constant-assertion cases, including nested/type-only identities,
  alignment guarantees, allowzero refinement and string-sentinel weakening.
- **Validation**: frozen GCC -Werror ASan/UBSan/reference suite passes 612 units,
  final 801 goldens, 70 ordinary/generated programs and 181 safety cases, plus every
  existing lifecycle/driver/storage/ABI/alignment/volatile suite. All 487 array
  comparisons match actual Zig 0.17. Clang sanitizer units/goldens/12 array programs/
  both storage harnesses/181 safety cases and optimized GCC array/storage checks pass.
  The expanded function-body/nested-type fixture passes all three builds against Zig.
  Four 60s fuzzers and 20k standalone mutations/stage pass; irgen/sema replay repeats
  after expanding the provenance seed. No sanitizer error; retained corpora 39/94.
- **Limits**: no comptime mutation, mutable array globals, explicit sentinel syntax,
  slices, generic/comptime function evaluation, variadic arrays or volatile whole-array
  copies. Runtime addresses of comptime-only element types remain rejected, not physical
  zero-byte pointers. c3 is complete for the existing array/type system; c and M5.2 stay
  partial. Next is c4 mutable array globals; do not conflate it with immutable provenance.

### M5.2c3a Internal array calls/returns and runtime aggregate joins
- **What**: by-value arrays in internal direct, recursive and indirect calls; nested,
  zero-bit/empty arrays and array-valued runtime if/labeled-block/while results.
  Array return/break snapshots survive cleanup mutation. Scalar and C-pointer ABI
  behavior remains unchanged; extern/export by-value arrays are rejected by language
  rules, with lazy validation only on export or actual runtime use.
- **Interactions**: source function types remain array-typed. Physical array args use
  byte-aligned transport pointers and private ABI-aligned callee snapshots; returns
  use a trailing caller-owned destination and void QBE return. A trailing hidden
  argument preserves source parameter positions for noreturn callback coercions.
  Join exits copy into owned slots and branch through void merges, never scalar array
  temporaries. All new slots share the c2 checked stack budget. This is a private
  convention, not compatibility with the official Zig compiler's native binary ABI.
- **Tests**: three native/reference call/control/callback programs; 24-width and nested
  argument/return/join roundtrips; TIR/SSA goldens and a lazy extern golden; promote the
  nested-return NYI; nine type/C-ABI/budget errors; an eight-argument noreturn callback
  safety case. The expanded array matrix adds 54 runtime/C-boundary/lazy-use cases.
- **Validation**: final GCC -Werror ASan/UBSan/reference suite passes 599 units,
  791 goldens, 67 ordinary/generated programs, 181 safety cases and all existing
  storage/driver/ABI/alignment/volatile suites. All 382 array comparisons match Zig.
  Independent Clang sanitizer/native tests and optimized array roundtrips pass. Four
  final 60s fuzzers and 20k standalone mutations per stage pass with no sanitizer error.
- **Limits**: general comptime element-pointer provenance, mutable array globals,
  explicit sentinel syntax, volatile whole-array copies, slices, generic/comptime
  calls, variadic arrays and broader array-child coercions remain pending. c3 and the
  enclosing milestones remain partial.

### M5.2c2 Mutable aggregate slots, initialization and copies
- **What**: mutable local arrays, mixed/runtime and nested literals, snapshot loads,
  whole-array assignment/copies and real runtime element addresses/lvalues. Copying
  an array preserves values across later mutation; scalar/pointer/callback/bool/u0/
  void leaves reuse existing layout/memory code. Empty arrays use zero-byte copies.
- **Interactions**: explicit owned indirect aggregate operands keep arrays out of scalar
  QBE temps. Borrow nested array pointers through index side effects; element pointers
  retain qualifiers/alignment. ARRAY_COPY emits overlap-safe memmove. Anonymous literal
  assignment writes fields in place; typed/@as literals initialize private temporaries,
  matching official 0.17 execution. GCC/Clang operand materialization is explicitly
  sequenced. Per-function aggregate slots include over-alignment padding in a checked
  16 MiB budget; layout/leaf/budget errors do not become truncated allocations.
- **Tests**: three native/reference executable fixtures; C interop for over-alignment,
  unaligned snapshots and both partial-overlap directions; three NYI-to-positive TIR
  promotions; nine new error goldens; four additional runtime bounds safety cases;
  44 new mutable-array/copy/coercion comparisons in the existing array matrix.
- **Validation**: 599 units, 779 goldens, 64 ordinary/generated programs, 180 safety
  cases and all storage/driver/alignment/volatile/ABI suites pass (GCC ASan/UBSan,
  reference-enabled); Clang sanitizer/native coverage and optimized array checks pass.
  All 328 array comparisons match Zig. Four 60s fuzz runs plus final semantic-guard
  fuzzing and 20k standalone mutations per stage pass; no sanitizer finding.
- **Limits**: no by-value array argument/return ABI or runtime array joins, general
  comptime element-pointer provenance, volatile whole-array copies, mutable array
  globals, slices or explicit sentinel syntax. c3 and the enclosing milestones remain
  partial; runtime addresses do not pretend to solve comptime provenance.

### M5.2c1 Immutable array backing and checked scalar runtime reads
- **What**: known array values now have actual immutable static backing when passed
  by address or dynamically indexed. Emit nested integer/bool/pointer/void/u0 arrays
  with true element strides; recursively retain pointer/byte-object relocations and
  reachable callbacks. Scalar reads through single array pointers work with constant
  or runtime indices, including strings, zero-bit elements and volatile pointers.
- **Representation/interactions**: TIR_ARRAY_ELEM_PTR carries an array pointer and
  usize index, with an element pointer result preserving const/volatile/allowzero and
  conservative alignment. Codegen compares unsigned indices against ordinary length
  (`>=`) or sentinel length (`>`), then multiplies by the checked element stride.
  Known indices are checked during sema. Loads reuse scalar/volatile memory lowering;
  narrow loaded representations normalize to their logical width, including signed
  i7/i33 and bool. Aggregate values never masquerade as QBE scalar temporaries.
- **Layout/storage**: sema_type_size now reports success/overflow through a checked
  output API; multiplication/sentinel-count overflow cannot become a wrapped size.
  Zero-bit arrays retain zero physical size even at maximal type lengths. Read-only
  arrays use .rodata; recursively pointer-bearing arrays use .data.rel.ro, with real
  over-alignment through the assembly companion. Nested immutable-value dependency
  and callback reachability walks memoize visited ValueIds rather than exponentially
  revisiting shared aggregate DAGs. Static array emission has an explicit total
  16,777,216-byte budget per compilation, in addition to the existing dense-value
  work budget; oversize/budget/layout limitations diagnose NYI and never emit a
  successful truncated object. Mutable globals/scalar backing retain existing behavior.
- **Grounding**: actual Zig 0.17 Type.zig abiSize/abiAlignment and Sema.zig elemValArray/
  elemPtrArray confirm stride, zero-bit layout, empty/bounds rejection and sentinel
  comparisons. Reference execution confirms a subtle distinction: known zero-bit or
  wholly undefined array *values* fold before a dynamic bounds check, whereas runtime
  pointer accesses retain bounds checks even for u0/void. Preserve that distinction.
  C interoperability tests use canonical narrow representations verified with Zig;
  an exploratory noncanonical-byte fixture failed the reference and is not treated
  as a supported C-to-Zig ABI contract.
- **Tests**: add 27 checked-layout units, three stage goldens, a positive promotion of
  the old runtime-index NYI, four TIR diagnostics and two backend-limit diagnostics.
  Backend error goldens now have an explicit harness route. The array matrix expands
  to 284 actual reference comparisons; generated widths include runtime pointer reads
  across 24 signed/unsigned types. Add ten actual reference panic cases, ordinary
  callback/volatile/string/OPV readbacks, and seven physical storage/C-ABI checks.
  The storage harness verifies bytes, section membership, local symbols, 256-byte
  alignment, PIE, full RELRO and absence of TEXTREL. Compiler subprocesses use
  bounded process-group cleanup. Final fresh GCC/Clang -Werror ASan/UBSan pass
  599 unit checks, 770 goldens and 61 ordinary/generated inputs. GCC full reference
  executable/storage/alignment/ABI/volatile/safety suites pass, as do Clang native
  executable/driver/storage/volatile/safety subsets. All 176 reference panic cases
  pass. Optimized GCC passes three array inputs and all seven storage cases. Four
  final 60-second fuzz targets and 20,000 mutations per stage pass (irgen 36/sema 88).
  All eleven semantic matrices pass 4,899 actual comparisons (4,615 existing plus
  284 arrays); no ignored failures/retries. Detailed scopes and prior CI/reference-probe
  limitations are in HANDOFF.md.
- **Reference probe limitation**: materializing a maximal undefined u0 array again
  stalled reference emission; an initial physical-suite run timed out after 120 seconds
  and remains a failed exploratory run. Final physical fixtures use a three-element
  zero-bit object and are actually compiled/executed with Zig; maximal lengths remain
  covered separately in checked-layout units and type-only pointer probes. This is
  not a reference language rejection, ignored timeout or automatic retry policy.
- **Next/limits**: immutable supported-element backing only. Mutable array variables,
  runtime aggregate elements/loads/copies, nested runtime aggregate indexing, element
  addresses/provenance and by-value array ABI remain NYI. Arrays do not gain typed
  address-dependency cycles or general comptime pointer interpretation here. Explicit
  sentinel syntax, slices/many-pointers and array/slice for integration stay pending.
  M5.2 remains partial; Q6 stays deferred to M10.2. Batch easy related additions, not
  this next aggregate-memory design without independent validation.

### M5.2a–b Compile-time array foundation
- **What**: fixed-size array types with full 64-bit usize lengths, typed and inferred
  `[_]` literals, expected-type/nested anonymous array initialization and child integer
  cast result locations. Compile-time indexing, type-derived `.len`, and `++` fold
  without runtime aggregate instructions. Readbacks of strings and mixed string/array
  concatenation preserve the zero sentinel slot; whole-array string dereferences now
  produce usable immutable aggregates. Arrays of supported integer/bool/void/type/
  callback values, empty arrays and partly undefined elements are covered.
- **Representation/interactions**: VALUE_ARRAY interns an immutable ordered sequence
  of child ValueIds, deep-copying backing storage. TypeArray.length is uint64_t and both
  words participate in type identity. New ARRAY_TYPE/ELEM_TYPE/INIT/INDEX/CAT and FIELD_VAL
  IR operations preserve expected element types; no parser or aggregate backend shortcut
  is added. Concatenation uses child peer types, retains zero sentinels when either
  operand has one, and returns a const pointer if an operand is a pointer. Fully defined
  u8 pointer results reuse VALUE_BYTES, including existing panic messages; undefined
  elements stay undefined. Whole-array coercion does not integer-widen elements, and
  pointer-to-array coercion retains existing mutable/const compatibility restrictions.
- **Grounding**: actual Zig 0.17 Sema.zig array-type/init/element/concatenation routines
  and real reference cases validate child restrictions, literal counts, sentinel bounds,
  typed-undefined coercions and concatenation peer types. A huge undefined zero-bit
  aggregate reference probe stalled, including a separate 15-second bounded check;
  this is not a language rejection. Final executable fixtures use undefined pointers
  to huge arrays instead: their type-derived lengths and 64-bit type identity compile
  and run successfully with Zig without materializing those aggregate values.
- **Tests**: fresh GCC/Clang -Werror ASan/UBSan pass 572 unit checks and 761
  goldens; 60 ordinary/generated executable inputs pass, including actual Zig
  emission/execution. GCC full executable/driver/storage/alignment/ABI/volatile/safety
  suites pass with and without reference checks; final Clang executable/driver/storage/
  volatile/safety subsets pass. Optimized GCC passes both array executables and all
  15 physical storage checks. The new semantic array matrix matches all 212 real Zig
  cases; 17 invalid fixtures reject with Zig and six explicit NYI fixtures are valid
  reference programs. CI includes the matrix and generated width readbacks. Lifecycle
  regressions pass 8 checks and adapter wiring passes 18. All eleven actual semantic
  matrices pass 4,827 cases (4,615 existing plus 212 arrays). One initial local pool
  failed to start szc while its executable was being replaced (Permission denied),
  not on a semantic mismatch; that pool remains a failed run. A separate full rerun
  of all 864 allowzero cases passes on the stable binary, as does the final 212-case
  array rerun. No filtered/skipped cases or automatic retries. Final forced fuzz rebuilds pass all
  four 60-second libFuzzer targets and 20,000 standalone mutations per stage
  (35 irgen/85 sema retained corpus inputs).
- **Limits/next**: dense constant construction has an explicit per-compilation budget
  of 1,048,576 element work units, charged across literals, concatenation and aggregate
  copies/byte loads. Exceeding it is NYI, not a Zig-invalid program or silently truncated
  length. Type-only lengths and undefined-pointer `.len` do not allocate dense elements.
  Runtime array elements/variables/indexing/storage/copies/by-value ABI and element
  addresses, explicit array sentinel syntax, untyped anonymous tuple inference and
  mixed bare-function/pointer element joins remain NYI. Do not turn an element address
  into a reference to a copied value. Array/slice for integration and lazy/repeated
  constant representations remain future work; M5.2 stays partial. Q6 remains M10.2.

### CI follow-up: uniform semantic compilation deadlines
- **Evidence**: 967329f/run 38711782475 passed compiler/fuzz/native/reference safety
  suites but reference Zig exceeded the nested-pointer matrix's 30-second deadline
  in case 89. Source/command/output were retained; later runner shutdown was also
  recorded. No accept/reject mismatch was reported, and runner delays remain incompletely
  diagnosed. The exact source and job evidence are recorded in HANDOFF.md.
- **What**: align the outlier nested-pointer compilation budget with the existing
  120-second budget of other semantic matrices. Preserve -j2, whole-group timeout
  cleanup, actionable failure context and strict exit checking. Runtime execution
  remains bounded at 15 seconds. No retries or ignored failures.
- **Tests**: shared lifecycle/adapter regressions pass, with uniform deadline assertions.
  All 168 real nested-pointer comparisons pass against Zig. No language milestone
  is advanced by this infrastructure-only checkpoint.

### M5.1c2b2b3c Typed immutable declaration address dependencies
- **What**: separate explicit const type bodies from initializers. Publish an immutable
  declaration address after its type/alignment resolve, without forcing its value.
  Self/mutual address-only dependencies work; actual value loads resolve the initializer
  and still reject dependency loops. Known values, pointer identity and lazy unused
  declarations are preserved; address-only uses still validate initializers/callbacks.
- **Representation/interactions**: SemaDecl caches the resolved declared type, GlobalId
  supplies a stable named address, and SemaGlobal.is_const selects immutable ownership.
  VALUE_GLOBAL now represents either kind of declaration address. Const values remain
  known Operands, never mutable-global loads or fake undefined placeholders. Interned
  values/objects stay immutable. Existing queue draining finishes initializers/reachable
  functions; known immutable pointer loads fold while runtime arguments still load.
  Named objects use .rodata/.data.rel.ro, including the above-64 assembly path; existing
  anonymous/local/inferred constant refs keep their acyclic interned-object representation.
- **Grounding/tests**: actual 0.17 Sema.zig zirDeclRef/analyzeNavRefInner and emitted probes
  confirm type-only addresses, fully resolved value loads, OPV behavior and rejected
  inferred/value/type/alignment cycles. Promote the prior self-address NYI into a positive
  TIR fixture. Add three stage goldens, runtime pointer readbacks, six reference-rejected
  errors and 187 differential cases (519 global-constant comparisons total). ELF storage
  checks verify all eight named immutable objects, real 256-byte alignment, local symbols,
  sections, full RELRO and no TEXTREL under PIE. The physical variant also runs with Zig.
  All ten actual differential matrices pass 4,615 cases. Fresh GCC/Clang -Werror
  ASan/UBSan pass 558 unit checks, 735 goldens, 58 executable inputs, driver/storage/
  safety/volatile suites. GCC full e2e is reference-enabled; detailed scope is in
  HANDOFF.md. Four 60-second fuzz targets and 20,000 standalone mutations per stage
  pass (irgen 34/sema 81 retained inputs). Optimized GCC also passes the new executable
  and physical storage checks. CI runs existing harnesses with the new coverage.
- **Limitations/next**: explicitly typed supported scalar/bool/single-pointer/void storage
  only. Inferred cycles and real value/type/alignment dependencies remain errors with
  primary-only diagnostics. No aggregate/float/comptime-interpreter expansion or change
  to exported/extern/threadlocal restrictions. Aligned local function aliases retain the
  documented upstream-emission-crash NYI. All enclosing M5.1 integration/alignment items
  remain partial. Batch related easy next steps where practical; Q6 stays at M10.2.

### CI follow-up: differential compiler process lifecycle
- **Evidence**: 72914b5 push CI passed; scheduled run 38675456100 later passed
  GCC/Clang/fuzz and all reference safety cases, then timed out after 30 seconds
  in the nested-pointer reference comparison. Its old subprocess.run cleanup only
  killed an immediate child, and no case/source context was retained. No compiler
  acceptance disagreement was reported; hosted-runner stalls are not fully diagnosed.
- **What**: extracted the already-tested group-cleanup runner into
  tools/compiler_process.py, shared by safety and all ten semantic matrices. Added
  per-actor/case progress and source/command/status/output failure reports; a timeout
  still fails immediately after whole-group cleanup, without retries or skipping.
  Reference matrix compilations use -j2, preserving every existing timeout budget
  (30s nested-pointer, 120s other compilation matrices). Parser comparisons unchanged.
- **Tests**: 17 compiler-wrapper/adapter regressions, including real matrix CLI
  crashes outside the repo cwd and mocked wiring checks for all ten adapters,
  plus the existing eight shared process-cleanup tests.
  e2e-test and safety-test run both. Compiler sources/corpora are unchanged.
  All ten actual matrices pass 4,428 Zig comparisons. GCC/Clang -Werror sanitizer
  suites pass 558 units, 726 goldens, executable/driver/safety checks; GCC's full
  e2e is reference-enabled. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Exact coverage is in HANDOFF.md; check new push CI
  separately rather than claiming hosted stalls have been solved.
- **Language follow-up**: actual Zig 0.17 semantic-only compilation accepts an
  aligned local bare-function alias, but emitted compilation SIGSEGVs; a function-
  declaration-aligned control succeeds. Keep the explicit alias-address NYI rather
  than fabricating backend guarantees. No language checkbox advanced here; next
  scoped language work was typed immutable address dependencies (now implemented above).
  Q6 stays at M10.2.

### M5.1c2b3b Observable volatile loads/stores
- **What**: TIR_LOAD_VOLATILE/TIR_STORE_VOLATILE distinguish runtime observable
  accesses from ordinary memory. Sema keeps volatile reads even when discarded,
  carries the flag through compound assignments/joins/calls, and preserves the
  usual const-store, undefined-pointer and comptime diagnostics. Known immutable
  values and one-possible-value void/u0 still fold as in Zig's analyzeLoad/storePtr2.
- **Backend**: QBE has no volatile load/store instruction. Each access calls a
  private outlined QBE helper containing exactly one width-correct load or store;
  QBE calls are opaque side effects and cannot be removed, merged or reordered as
  ordinary loads. No interprocedural inlining/LTO is introduced. Load results are
  normalized for signedness/subword integer widths; x86_64 handles weak alignment.
  Standalone emit-qbe includes helper definitions and needs no assembly companion.
  These calls conservatively inhibit optimization; they are not atomic operations
  or a promise of cross-thread memory ordering.
- **Physical tests**: a Linux/x86_64 mprotect/single-step observer counts actual
  memory faults and records access order. Forty-four sequences cover 17 scalar/
  bool/pointer/callback-slot types at aligned and offset-one addresses, discarded
  reads, consecutive stores, loops, compound assignment, &p.* cancellation and
  void/u0 no-access behavior. The same oracle passes with Zig Debug/ReleaseFast.
  Compound assignment accepts either separate read/write instructions or LLVM's
  single read-modify-write instruction (reported as a write fault). Undefined
  stores have no cross-compiler access-count assertion: ReleaseFast can omit them;
  szc currently emits an arbitrary zero store. No undefined contents are read.
- **Regression coverage**: IR/TIR/QBE goldens, five reference-rejected diagnostics,
  an executable with globals, defer/loop/compound effects, indirect callback slots,
  known volatile comptime pointers and OPVs. Removed temporary access-NYI goldens;
  their fuzz inputs now exercise successful accesses. CI e2e includes the observer.
  Corpus grows to irgen 33/sema 79. GCC/Clang -Werror ASan/UBSan pass 558 units,
  726 goldens, complete native executable/memory/166 safety suites; GCC's full e2e
  is reference-enabled. Optimized GCC also passes goldens/executables/observer,
  final Clang retains all 317 new function/qualifier reference comparisons, and
  four 60-second fuzz targets plus 20,000 standalone mutations per stage pass.
  Exact totals and hosted CI status are recorded in HANDOFF.md.
- **Limits/next**: only existing supported value layouts; no atomics, fences,
  memory-mapped-device-specific access protocol, aggregate/vector/wide-int/float
  expansion or non-single pointers. The three-checkpoint batch is complete; return
  to small milestones. Function-alias alignment and constant address cycles remain
  explicit gaps in enclosing M5.1 alignment integration. Q6 stays at M10.2.

### M5.1c2b3a Volatile pointer qualification/coercion
- **What**: single-pointer syntax lowers IR_POINTER_VOLATILE. Interning already
  includes TypePointer.is_volatile; sema now populates and prints it. Coercion can
  add but not discard volatile, while mutable outer pointees require bidirectional
  compatibility. Joins combine const/allowzero/volatile and retain the weakest
  alignment only when both operands coerce. Function parameter variance uses the
  same memory-compatibility checks. Pointer identity and storage layout do not change.
- **Grounding/tests**: Sema.zig coerceInMemoryAllowedPtrs and peer resolution
  confirm the qualifier rules. A 240-case Zig matrix covers direct qualifiers,
  alignment interactions, nested mutable invariance, joins and function variance.
  Added three stage goldens, seven reference-rejected diagnostics, executable
  identity/slot/address-cancellation coverage and two temporary access-NYI fixtures.
  Removed the old blanket volatile-pointer rejection. CI runs the new matrix.
  GCC -Werror ASan/UBSan passes 558 units, 720 goldens and full executable/memory/
  safety suites. Clang sanitizer checks and all 240 Zig comparisons pass; the new
  executable passes with Zig too. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. Exact test counts are in HANDOFF.md.
- **Safe boundary**: this checkpoint supports qualification only. Volatile loads
  and stores fail explicitly rather than silently becoming ordinary QBE memory
  operations. The next checkpoint removes these temporary guards and supplies
  observable access lowering. Atomicity/fences, casts, non-single pointers and
  unsupported pointee types remain separate work. Q6 is still deferred to M10.2.

### M5.1c2b2b4 Function declaration/address alignment
- **What**: function declarations have isolated alignment bodies/source locations.
  SemaFunc stores validated code alignment independently of the interned function
  signature; taking/decaying the address preserves it in the pointer type. Alias
  values retain their function identity. Pointer weakening and joins reuse the
  existing memory-coercion rules. Function *types* cannot carry an alignment,
  matching std.zig.AstGen; Sema's u29/nonzero/power-of-two checks are shared.
- **Backend**: QBE functions are already 16-byte aligned. Larger guarantees put
  each function at offset zero of a private executable .text section; the assembly
  companion establishes its ELF section alignment before the QBE body. The driver
  concatenates complete assembly streams without parsing or patching QBE output.
  This avoids padded wrappers, changed function identity and huge trailing padding.
  Standalone emit-qbe explicitly rejects code alignment above 16; build-exe/run work.
  Exported definitions are emitted aligned; extern declarations trust their supplier.
- **Tests**: 27 physical internal/export/extern address checks through 4096 bytes,
  indirect calls via immutable pointer slots, ELF executable-section/no-TEXTREL and
  bounded-size assertions, 77 reference address/alias/join comparisons, three stage
  goldens, twelve reference-rejected diagnostics and executable regression. CI runs
  the physical and differential tests; corpus grows to irgen 31/sema 70 inputs.
  GCC/Clang -Werror sanitizer checks pass; GCC's full reference-enabled suite passes
  558 units, 709 goldens and all executable/memory/166 safety cases. Four 60-second
  fuzz targets and 20,000 standalone mutations per stage pass. See HANDOFF.md for
  exact Clang coverage; hosted CI is not implied by local success.
- **Limits/next**: explicit alignment on a local bare-function alias remains an
  honest NYI rather than a metadata-only promise; inline addresses/generic calls,
  explicit callconv/addrspace/linksection and other existing restrictions remain.
  Constant type-only address cycles remain unsupported. Enclosing memory/alignment
  milestones stay partial. This turn batches volatile qualification then volatile
  loads/stores next; Q6 remains deferred to M10.2.

### M5.1c2b2b3b Aligned internal global constants
- **What**: aligned global constants retain their original comptime Operand and
  unique immutable object identity. IrDecl has a separate type body for explicitly
  typed aligned constants and an isolated alignment body; SemaDecl stores validated
  byte alignment separately. Declaration references pass it to analyze_ref, rather
  than manufacturing runtime variables or discarding known values.
- **Reference semantics**: explicit type resolves before alignment/initializer;
  inferred type follows initializer resolution before alignment. Zig 0.17
  Zcu/PerThread.zig and Sema.zig source/probes confirm this ordering and the global
  rejections for comptime-only types and bare function aliases. Ordinary copied
  globals/locals use their own natural storage alignment. Unused declarations stay
  lazy; invalid attributes and invalid initializers keep diagnostic ordering.
- **Storage/interactions**: existing aligned immutable objects emit QBE data up to
  64-byte alignment, or outlined .rodata/.data.rel.ro assembly above that. Real
  addresses, nested pointers, mutable pointees, callback slots, void/u0 and
  zero/undefined storage work without changing backend representation. Standalone
  emit-qbe retains its above-64 static-data limitation; build-exe/run supply assembly.
- **Tests**: 153 new physical address/readback checks with PIE/RELRO assertions,
  332 comparisons against Zig, IR/TIR/QBE goldens, and 21 diagnostic fixtures.
  Twenty diagnostics are reference-rejected; the typed constant self-address cycle
  records an existing szc-only limitation which the reference accepts. Sanitized
  replay matches all 332 new cases plus 300 mutable-global and 626 local-constant
  cases. Full validation totals are recorded in HANDOFF.md; CI runs both the new
  physical tests and reference matrix. GCC/Clang -Werror ASan/UBSan pass 558 unit
  checks, 694 goldens, all executable/memory suites and 166 safety cases. GCC also
  passes the full reference-enabled suite. Four 60-second fuzz targets and 20,000
  standalone mutations per stage pass. Corpus grows to irgen 30/sema 65 inputs.
- **Limitations/next**: internal constants of supported runtime-representable types
  only; exported/extern/threadlocal aligned constants remain explicit NYI. Typed
  constant address-dependency cycles were unsupported at this checkpoint;
  M5.1c2b2b3c above implements them without placeholder values. Function declaration/address alignment is next; existing
  aggregate/float/backend restrictions remain. All enclosing alignment and memory
  milestones stay partial. Q6 remains deferred until M10.2.

### CI follow-up: procfs reaping race in cleanup regression
- da77561 CI exposed ESRCH while reading a just-reaped descendant's procfs file.
  The probe now recognizes both ENOENT and ESRCH as disappearance, without hiding
  live processes or changing production timeout handling. Deterministic tests cover
  both errors and zombie/sleeping states; all eight cleanup checks pass locally.
  No language milestone was advanced. Check the new push before claiming CI green.

### CI follow-up: safety subprocess-group timeout cleanup
- **What**: safety commands now start in a new session. On a compilation/execution
  deadline, the harness kills the whole process group, drains captured output and
  raises TimeoutExpired with the command, original budget and captured bytes.
  Successful/nonzero exits and signal statuses retain subprocess.run semantics.
- **Why**: fc00389/run 38611434252 passed GCC/Clang/fuzz, but differential safety
  completed 159/166 cases with seven timeouts: four native compilations, two
  reference compilations and one reference execution. The -j2 reference cap alone
  did not prevent stalls. The old subprocess.run timeout path only killed its
  immediate child; inherited pipes from descendants could keep cleanup waiting.
  This fixes that reproducible harness defect, not a claim that all hosted-runner
  stalls have been diagnosed or that remote CI has recovered.
- **Regression coverage**: tests/run_process_timeout.py checks exact stdout/stderr
  and nonzero status, signal status, a waiting parent with a long-lived child,
  and an already-exited parent whose child still owns the output pipes. Timeout
  cases must return promptly with the expected partial output and no live child.
  e2e-test and safety-test both run these four checks before compiler safety cases.
- **Validation**: GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 671 goldens,
  53 ordinary executable inputs, four process-cleanup checks and 166 safety cases.
  The complete GCC reference-enabled e2e suite also passes existing driver, storage,
  ABI and local/constant/global alignment checks; all 166 reference safety programs
  pass with the new helper. Four 60-second fuzz targets and 20,000 standalone
  mutations per stage pass. No compiler semantics,
  reference backends, expected panics, -j2 job cap, 120-second compilation budget
  or 15-second execution budget changed. No timeouts are ignored or retried.
- **Next**: check push CI; if stalls persist, inspect its new evidence rather than
  raising budgets blindly. Language work remains aligned global constants, then
  function alignment; all enclosing memory milestones stay partial.

### M5.1c2b2b3a Internal mutable global alignment
- **What**: internal mutable globals accept explicit alignment with declared or
  inferred types. IrDecl carries a separate isolated alignment body/source location.
  Type and alignment resolve before publishing a typed global's address; inferred
  declarations resolve the initializer/type first, then alignment. SemaGlobal keeps
  byte alignment independently of its value type, and the published pointer retains
  explicit natural alignment as well as weak/strong guarantees.
- **Lazy resolution/identity**: DECL_TYPED reuses the already validated address and
  alignment while deferred initialization drains. Forward references, address cycles
  and invalid initializers reached only through an address remain checked. An
  unused invalid alignment remains lazy. Alignment expressions cannot load mutable
  global contents at comptime or leak runtime execution into attribute evaluation.
- **Codegen/storage**: ordinary QBE data records use the actual explicit alignment.
  Above QBE's static limit of 64, the existing assembly companion emits writable
  .data with .balign; zero/undefined storage uses .zero. Pointer/function-pointer
  relocations stay writable, not in the immutable .data.rel.ro section. Global
  address identity is unchanged by weakening a pointer type.
- **Reference grounding**: Zig 0.17 Zcu/PerThread.zig analyzeNavType resolves declared
  type/layout then pointer modifiers; analyzeNavVal handles modifiers after inferred
  initializer/type resolution. Typed/inferred error-order fixtures, pointer type
  probes and executable behavior were checked against the actual reference binary.
- **Tests**: added IR/TIR/QBE goldens, 15 reference-rejected diagnostic cases and an
  explicit global-constant NYI fixture. The physical matrix checks 153 global
  addresses/readbacks across weak/natural/over-alignment, including zero and
  undefined storage, pointers, callbacks and OPVs, with no TEXTREL. The reference
  coercion matrix covers 300 typed/inferred global cases and is in CI. Executable
  coverage includes forward address cycles, lazy invalid declarations, defer writes
  and mutations through aligned pointer/function-pointer slots. GCC/Clang -Werror
  ASan/UBSan pass 558 unit checks, 671 goldens, 53 ordinary executable inputs plus
  two szc-only inputs, 16 driver, 11 storage, four ABI, seven unaligned, 135 local,
  270 constant, 153 global address and 166 safety cases. GCC reference integration
  and optimized GCC checks pass. The 300 global comparisons match Zig and sanitized
  replay; existing 626 constant cases retain sanitizer parity. Four 60-second fuzz
  targets and 20,000 standalone mutations per stage pass. Corpus: irgen 29, sema 58.
- **CI follow-up**: prior 8bf59f5 passed GCC/Clang/fuzz, but reference Zig timed out
  after 120 seconds on shl_size_33/shr_size_33 (164/166 safety cases completed).
  Safety reference compilations now pass -j2 to bound worker parallelism, retaining
  the 120-second compilation and 15-second execution budgets and all assertions.
  Both affected reference programs compile in under one second locally with -j2
  and produce the expected panic; remote success still requires checking push CI.
- **Limitations/next**: only internal mutable globals. Global const alignment,
  exported/extern/threadlocal variables and function declaration/address alignment
  retain their explicit limitations. Do not reuse global-variable storage for known
  constants or silently promise alignment on code symbols. Enclosing alignment and
  memory milestones stay partial; Q6 remains deferred until M10.2.

### M5.1c2b2b2 Aligned local constants
- **What**: explicit local const alignment preserves the immutable binding's value,
  including comptime integers/types and known pointer values. IR_CONST_BIND carries
  the initializer and validated IR_ALIGN ref without converting reads into runtime
  loads. Cached IR_REF instructions remain anchored after the binding, so runtime
  values spill once at the definition even when first addressed in one branch.
- **Known storage**: ConstObject records alignment alongside value and identity;
  hashing/equality/rehashing include it. Compatible pointer casts retain the same
  object. Known dereferences still recover the original value. Codegen uses the
  object's real storage alignment, not the alignment of the pointer value itself.
  Pointer/function-pointer objects remain in .data.rel.ro; scalars remain in .rodata.
- **QBE/driver interaction**: the pinned QBE data parser accepts powers of two only
  through 64 (its alignment field is bounded by CHAR_MAX). Larger immutable objects
  are emitted as ELF assembly data with .balign, correct scalar widths and symbol
  relocations. Codegen writes an optional assembly companion; the driver keeps it
  in an anonymous temporary, runs external QBE normally, appends the data to QBE's
  assembly and invokes cc. No text rewrite, fake alignment promise or runtime
  address rounding substitutes for a known object's relocatable aligned address.
  --keep-temps retains the complete .s; success/failure cleanup remains unchanged.
- **Standalone IL**: emit-qbe reports a compile diagnostic when a materialized
  constant requires alignment above 64; use build-exe/run for those programs.
  Ordinary standalone IL is unchanged. The executable harness explicitly tests
  this limitation for marked outlined-data inputs, then tests the full pipeline.
  The sema fuzzer exercises both codegen output streams through its discard sink.
- **Tests**: added IR/TIR/QBE goldens, reference-rejected alignment diagnostics,
  interning/rehashing unit checks, retained-assembly and tool-failure driver checks.
  Assembly checks cover 270 physical known/runtime constant addresses across 15
  types and nine alignments, plus readbacks and PIE/RELRO checks. Executable tests
  cover runtime branch dominance, recursion, loop initialization, returned known
  addresses, nested object dependencies and function-pointer storage. The reference
  matrix covers 626 coercion/comptime cases, including copied binding alignment.
  GCC/Clang -Werror ASan/UBSan pass 558 unit checks, 652 goldens, 52 ordinary
  executable inputs plus two szc-only inputs, 16 driver, 11 storage, four ABI,
  seven unaligned memory, 135 mutable-local alignment, 270 constant alignment and
  166 safety cases. Full GCC reference checks and optimized GCC checks pass.
  All 626 new comparisons match Zig and sanitized replay; existing 300 mutable
  cases retain sanitizer parity. Four final 60-second fuzz targets and 20,000
  standalone mutations per stage pass. Corpus: irgen 28, sema 52 inputs.
- **Limitations/next**: aligned global declarations and function declarations remain
  pending. Taking the address of an explicitly aligned local bare-function alias
  is an explicit NYI: metadata alone must not promise stronger code alignment.
  Calling that alias and addressing an aligned function-pointer *slot* work.
  General comptime interpretation, floats and wider/aggregate runtime storage keep
  their existing limits. All enclosing memory/alignment milestones stay partial.

### M5.1c2b2b1 Aligned mutable locals
- **What**: mutable local declarations accept explicit integer alignment, including
  inferred types, weak alignment and explicit natural alignment. A new IR_ALIGN
  instruction evaluates an isolated comptime body before the initializer; this
  keeps invalid attributes observable even when initialization fails or terminates.
  Pointer types and declarations share alignment-body generation and validation:
  defined u29, nonzero power of two, no captured runtime execution.
- **Storage/codegen**: IrAlloc carries the validated alignment ref; sema retains it
  in the slot pointer type instead of changing the child's ABI layout. QBE uses
  alloc4/8/16 for alignments up to 16. Larger alignments privately over-allocate
  max(size, 1) + alignment - 1 bytes and round the address upward with add/and;
  the entire object remains inside its allocation. Slots remain per activation,
  hoisted to function entry; initializers still execute at their declaration sites.
- **Interactions**: explicit natural alignment remains type-distinct from implicit
  alignment, and address coercions can weaken, never strengthen, the guarantee.
  Pointer/function-pointer slot alignment is independent of pointee alignment.
  Recursive activation identity, loop reinitialization, aliases, defer writes,
  void/u0 storage and weak-alignment scalar accesses have executable coverage.
- **Reference grounding**: Zig 0.17 Sema.zig zirAllocExtended resolves declaration
  alignment before initialization and retains explicit alignment in pointer flags.
  Acceptance/type-identity probes and invalid-attribute fixtures were checked
  against the actual 0.17.0 binary, not inferred from QBE's allocation primitives.
- **Tests**: added IR/TIR/QBE goldens, 12 reference-rejected diagnostic cases and an
  explicit aligned-local-constant NYI fixture. The runtime assembly helper checks
  actual addresses for 15 value types at nine alignments (135 cases); the local
  alignment/coercion differential matrix has 300 cases and runs in CI. GCC/Clang
  -Werror ASan/UBSan pass 554 unit checks, 636 goldens, 51 ordinary executable
  inputs plus two szc-only inputs, 12 driver, 11 storage, four callback ABI, seven
  unaligned memory, 135 local alignment and 166 safety cases. GCC reference checks
  and optimized GCC goldens/executables/address checks pass. All 300 new cases
  match Zig and sanitized replay; the existing 1,035 pointer-alignment comparisons
  also match Zig. Four 60-second fuzz targets and 20,000 standalone mutations per
  stage pass. Corpus: irgen 26, sema 47 inputs.
- **Limitations/next**: this completes mutable locals only. Aligned local constants,
  globals and functions remain separate storage work; do not implement constants
  as runtime variables and lose comptime values/reference identity. Float alignment
  expressions, general comptime interpretation and other pointer attributes remain
  pending. All enclosing M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2/M5.1c2b2b stay partial.

### M5.1c2b2a Alignment-qualified pointer types and unaligned memory
- **What**: single-pointer types accept `align(integer_expression)`. IrPointerType
  holds a separate comptime body and source location for the alignment expression;
  sema evaluates it with a fresh operand-map overlay, no runtime TIR and no enclosing
  function/control targets. Values must be defined u29, nonzero powers of two.
  Captured runtime values, unreachable/panic and runtime stores cannot escape into
  runtime code from a type attribute. Calls/mutable comptime interpretation still
  have their existing explicit NYI limits.
- **Type identity/coercion**: TypePointer.alignment stores zero for implicit ABI
  alignment and byte units for explicit alignment. Explicit natural alignment is
  memory-compatible but not type-identical to implicit alignment in Zig 0.17; do
  not canonicalize those types together. Implicit effective alignment comes from
  the child ABI layout. Coercion may weaken alignment, never strengthen it; mutable
  outer pointees require equal effective alignment. Function parameter/return
  variance includes these rules, and typed undefined cannot bypass them.
- **Joins**: use the minimum effective alignment, retaining explicit alignment when
  either input was explicit, plus the existing const/allowzero union. Both inputs
  must coerce to the selected pointer. Bare function bodies decay before an aligned
  const-function-pointer join; mutable function-pointer peers remain rejected,
  matching reference behavior. Differential testing caught and fixed an initially
  over-permissive mutable-peer merge.
- **Backend/interactions**: existing x86_64 QBE scalar loads/stores operate on
  unaligned addresses; no invented alignment promises or dummy backing storage.
  Seven assembly-backed regressions supply addresses at offset one from aligned
  data and check signed/unsigned 16/32/64-bit loads/stores, an unaligned pointer slot,
  and untouched prefix/suffix guard bytes. They also execute with official Zig.
  Alignment of a pointer's pointee is separate from the pointer value's own ABI
  alignment (eight bytes); existing local/global slots and immutable objects retain
  natural storage alignment. Weakening a pointer never changes object identity.
- **Comptime safety**: auditing attribute evaluation found missing runtime-emission
  guards in coercions, integer casts, unary/bool operations, conditional branches
  and stores. These now reject runtime work in a comptime-only body instead of
  emitting through a null TIR. Runtime paths and OPV folding remain intact.
- **Reference**: actual 0.17 Sema.zirPtrType, validateAlign,
  coerceInMemoryAllowedPtrs, pointer peer merging and Type.abiAlignment, plus binary
  probes. Alignment expressions use the existing supported comptime integer subset;
  float literals such as align(4.0), which Zig accepts, remain explicit float NYI.
- **Validation**: GCC/Clang -Werror ASan/UBSan and optimized GCC: 554 unit checks,
  620 goldens, 50 ordinary/reference executable inputs plus two szc-only inputs,
  12 driver checks, 11 PIE/storage checks, four callback ABI checks, seven unaligned
  memory cases and 166 safety cases pass. Four 60-second fuzz targets passed, with
  final irgen/sema each re-fuzzed for 60 seconds after the comptime fixes.
  All 1,035 alignment comparisons match Zig 0.17.0 and sanitized replay matches
  debug output for every case. Existing 168 const-only pointer, 486 function-pointer
  and 864 allowzero comparisons also match. Standalone 20,000 mutations per stage
  pass, with irgen corpus 25 and sema corpus 43 inputs.
- **Limitations/next**: this is pointer type alignment, not explicit storage alignment.
  Local/global/constant/function declaration alignment remains M5.1c2b2b; do not
  claim over-aligned storage can already be declared. @alignCast and integer/pointer
  casts remain later casting work. Volatile memory access, address spaces and packed
  pointer fields are still pending. M5.1/M5.1c/M5.1c2/M5.1c2b/M5.1c2b2 remain partial.

### M5.1c2b1 Allowzero pointer qualification
- **What**: single-pointer syntax accepts `allowzero`, independent of `const`.
  IR_PTR_TYPE uses an IrPointerType payload with a child ref and qualifier flags;
  the text dump preserves existing const notation and appends allowzero. Type
  interning and diagnostic names retain the qualifier, including nested and function
  pointers. Ordinary address-of results and storage addresses remain nonzero types.
- **Coercion/joins**: nonzero pointers may widen to allowzero; implicit conversion
  cannot discard allowzero. Mutable outer pointees require bidirectional compatibility,
  preventing a nullable pointer from being stored through an alias of a nonzero slot.
  The same rule applies to typed undefined pointers. Branch joins combine top-level
  const/allowzero flags only if both inputs can coerce to the resulting pointer,
  selecting an existing memory-compatible child rather than inventing a new pointee.
  Function parameter contravariance and return covariance include allowzero.
- **Runtime/interactions**: existing pointer casts, scalar loads/stores, comparisons,
  writable globals, immutable backing objects and direct/indirect calls carry the
  qualification without changing the x86_64 representation. Void/u0 accesses remain
  no-ops, even through undefined pointers; other undefined dereferences still error.
  Runtime tests cover both branch orders, nested qualification, globals, immutable
  object identity, callback signatures and qualifier-sensitive type identity.
- **Reference**: verified actual 0.17 Sema.coerceInMemoryAllowedPtrs,
  resolvePeerTypes pointer merging, resolvePairInMemoryCoercible and Type.ptrAllowsZero;
  nine negative diagnostic fixtures were also checked with the official binary.
- **Validation**: GCC/Clang -Werror ASan/UBSan and optimized GCC: 554 unit checks,
  590 goldens, 49 ordinary/reference executable inputs plus two szc-only inputs,
  12 driver checks, 11 PIE/storage checks, four callback ABI checks and 166 panic
  cases pass. All 864 allowzero comparisons match Zig 0.17.0; sanitized replay
  matches debug output for every case. Existing 168 const-only pointer and 486
  function-signature comparisons also match. Four 60-second fuzz targets and
  standalone 20,000 mutations per stage pass (irgen corpus 24, sema corpus 34).
- **Limitations**: this checkpoint implements allowzero qualification, not integer
  pointer construction. @ptrFromInt/@intFromPtr/@ptrCast remain M5.5; there is no new
  null-pointer value or optional representation. Alignment, volatile memory semantics,
  address spaces, packed pointer fields and non-single pointer kinds remain pending.
  M5.1/M5.1c/M5.1c2/M5.1c2b remain partial; next is M5.1c2b2 pointer alignment.

### M5.1c2a Function types, addresses and indirect calls
- **What**: IR_FN_TYPE represents a function signature as a type value, without
  creating a function declaration or consuming a FuncId. Function declarations and
  type expressions share prototype lowering. TYPE_FN is comptime-only; function
  addresses are VALUE_FUNCTION values typed as single const pointers, pointing at
  the original function symbol rather than a scalar constant backing object.
  Function aliases therefore share address identity with their original declaration.
- **Calls/reachability**: callable signatures come from either function bodies or
  single pointers to functions. Known targets retain direct calls; runtime targets
  use TirCall.indirect and QBE `call %temporary`. Signature-driven argument coercion,
  arity checks, void/u0 erasure and noreturn termination are shared. Function symbols
  used in runtime values or nested immutable/global initializers queue their bodies;
  the existing function/global drain resolves newly reachable definitions. Callees
  are evaluated before arguments, preserving the old target when an argument changes
  a global callback. Known pointer dereference recovers the body; runtime dereference
  rejects loading a comptime-only function body instead of pretending to load bytes.
- **Signature identity/coercion**: parameter noalias flags and auto/C/inline convention
  identity are interned. Extern/export declarations infer C convention; explicit
  callconv expressions remain NYI. Non-pointer noalias parameters and auto variadic
  types diagnose. Memory-compatible function returns are covariant, parameters are
  contravariant, noalias/convention/arity are invariant, and mutable outer pointers
  require bidirectional compatibility. A noreturn return can widen only when the
  enclosing memory is not mutable. Same-representation integer aliases also match;
  integer-width widening is not function-pointer covariance.
- **Backend/ABI**: symbolic function initializers work in writable globals and
  immutable .data.rel.ro backing objects, with PIE/RELRO regression coverage. All
  integer/bool call results are normalized, including narrow C callback results whose
  upper return-register bits are unspecified. An assembly-backed test reproduced
  the old incorrect result, then checks unsigned/signed byte and boolean returns
  through both direct and indirect calls against Zig. Eleven QBE snapshots gained
  the expected canonical-return operations; no unrelated snapshots were regenerated.
- **Tests**: GCC/Clang -Werror ASan/UBSan, optimized GCC: 554 unit checks, 579 goldens,
  48 ordinary/reference executable inputs, two szc-only linkage inputs, 12 driver
  checks, 11 PIE/storage checks, four callback ABI checks and 166 panic cases. All
  486 function-signature/qualifier pairs and 168 existing pointer-qualifier pairs
  match Zig 0.17.0; function comparison is now in CI. Generated indirect-call tests
  cover all 128 signed/unsigned 1–64-bit integer types. Four 60-second fuzz targets
  and standalone 20,000 mutations per stage pass; final sema/codegen was re-fuzzed.
- **Reference/interactions**: checked actual Sema.analyzeRef, checkCallArgumentCount,
  coerceInMemoryAllowedFns/Ptrs and callconvCoerceAllowed, plus binary probes. Existing
  integer/defer safety and mutable-global lazy initialization remain intact. Imported
  libc callbacks use szc's cc linkage; their reference fixture was separately checked
  with Zig -lc rather than silently adding libc to every reference test.
- **Limitations**: pointer attributes remain M5.1c2b. Inline function addresses,
  runtime generic function addresses, comptime calls/specialization, explicit calling
  convention expressions and broader C ABI/aggregate/variadic support remain NYI/later
  work. Known generic function addresses may be discarded without forcing emission.
  M5.1/M5.1c/M5.1c2 are still partial; this is not a bootstrap-completion claim.

### CI follow-up: separate compilation and execution timeouts
- M5.1c1 CI run 38548134671 passed compiler and fuzz jobs; differential safety failed
  only because seven Zig reference compilations exceeded the 15-second execution
  budget. Safety compilation now has 120 seconds, while execution stays at 15 seconds.
  Compiler failures, runtime hangs and panic/message mismatches remain hard failures.
  Follow-up run 38549587896 was stopped by runner shutdown during the reference safety
  loop; its compiler/fuzz jobs passed. Per-case CI progress and failures now flush
  immediately, preserving the active case on interruption; no CI success is claimed.

### M5.1c1 Internal mutable scalar globals
- **What**: internal container-level `var` declarations now have stable GlobalId
  records, typed comptime initializers and writable scalar storage. VALUE_GLOBAL
  represents a known address, never a constant snapshot of the stored value.
  Explicit types have a separate IR type body; IR_DECL_TYPE supplies the initializer
  result context. DECL_TYPED publishes the address without forcing initialization.
  Executable analysis drains pending initializers and functions to a fixed point.
  Forward references, self-address initializers and address cycles work independently
  of first-use order, including an inferred global pointer into a typed cycle.
- **Memory/backend**: direct and indirect reads/stores reuse TIR_LOAD/STORE with
  materialized global symbols. Integer, bool, pointer, void and u0 globals are
  supported; zero-sized accesses remain no-ops. QBE emits internal writable data,
  zero-filled objects with `z`, and static relocations to mutable globals, immutable
  backing objects and literal bytes. Dependency marking includes global initializers
  before immutable-object closure; immutable pointer backing remains in .data.rel.ro.
  Unsupported wide integer storage diagnoses at the global declaration location.
- **Language behavior**: mutable global reads are runtime even when the initializer
  is known. Comptime reads/writes of non-OPV mutable storage fail, including undefined
  stores and indirect access; self-reads use Zig's declaration-dependency diagnostic.
  Void/u0 accesses need no memory and remain valid at comptime, with direct declaration
  resolution still detecting a self-dependent initializer. Unreferenced declarations
  stay lazy; exported declarations remain eager roots and explicitly diagnose NYI.
- **Tests**: 549 unit checks, 557 goldens, 45 ordinary/reference executable inputs
  plus one szc-only input, 12 driver checks, eight PIE/RELRO/storage checks and 163
  panic cases. The global generator covers all 128 signed/unsigned 1–64-bit integer
  types and grows global/pointer records. Runtime tests cover recursion, defer,
  pointer reassignment, live const pointers, address cycles and reference snapshots.
  Diagnostic probes agree with actual Zig 0.17.0; 168 qualifier cases still match.
  GCC/Clang -Werror ASan/UBSan, optimized GCC, four 60-second fuzz targets and standalone
  20,000 mutations per stage pass; the final lazy-resolution change was re-fuzzed.
- **Reference/interactions**: checked Sema.analyzeNavRefInner/ensureNavResolved's
  type-versus-fully distinction, declaration loads, OPV handling and actual binary
  probes. Existing reference dominance, const-object identity, arithmetic safety,
  panic byte storage and pointer qualification remain unchanged. Initializers are
  validated even when the global is only referenced by address.
- **Limitations**: this is internal scalar storage only. Extern/export/TLS globals,
  custom global align/linksection/addrspace, function pointers/calls, pointer attributes,
  aggregate globals and general mutable comptime memory remain explicit NYI/later work.
  Comptime calls needed for richer global initializers remain M7. M5.1 and M5.1c are
  still partial; next checkpoint is M5.1c2, not a claim of full bootstrap support.

### M5.1b Definition-anchored references and immutable object identity
- **What**: `&` now accepts local constants, parameters, ordinary temporaries and
  named global constants. A dense irgen reference table caches addresses by value
  instruction. Body finalization inserts IR_REF immediately after its definition,
  with parameter references at function entry. This preserves identity for aliases
  and dominance across branches, captures and deferred bodies without speculative
  use-site spills. Runtime storage is per invocation, not mutable static memory.
- **Comptime/storage**: VALUE_REF points to an interned ConstObject, not a raw host
  pointer. Anonymous immutable objects deduplicate by value; named declarations
  have separate identities, even for equal initializers. Pointer qualification
  preserves object identity. Known dereferences keep their comptime values,
  including type values; runtime references use existing TIR_ALLOC/STORE/LOAD.
  Codegen marks transitive object dependencies and emits only referenced storage.
  Scalar objects live in .rodata; pointer objects use .data.rel.ro so PIE relocation
  does not introduce TEXTREL and GNU_RELRO protects them after loader relocation.
- **Language behavior**: `==`/`!=` compare runtime pointers or known immutable object
  identities; single-pointer ordering stays invalid. Undefined pointer comparisons
  produce an undefined bool, accepted when discarded but rejected when used as a
  compile-time condition. Context can type temporary literals (`read(&1)`), but
  does not retype a named comptime_int binding (`read(&constant)` still diagnoses
  `*const comptime_int`). Writes through const references are rejected normally.
- **Tests**: GCC/Clang -Werror ASan/UBSan: 542 unit checks, 538 goldens, 42 ordinary/
  reference executable inputs plus one szc-only input, 12 driver checks, four new
  PIE/RELRO storage checks and 159 panic cases. The all-width generator now covers
  parameter/alias addresses and materialized min/max constants for all 1–64-bit
  integer types. Tests cover recursion, skipped-branch first uses, loop captures,
  cleanup reads, global identity, nested backing objects, context and type values.
  All 168 qualifier comparisons still match Zig. Optimized GCC, four 60-second fuzz
  targets and standalone 20,000 mutations per stage pass; final sema was re-fuzzed.
- **Reference**: checked actual 0.17 AstGen.ref_table/rvalue/body fixups, plus
  Sema.analyzeRef/analyzeNavRef/cmpSelf and binary probes. Global declarations and
  anonymous constant addresses are deliberately not conflated. Returning a runtime
  stack pointer is not diagnosed by the reference; tests never dereference one
  after its lifetime ends. Known pointer comparison poison is preserved, not
  eagerly diagnosed merely because a comparison expression exists.
- **Limitations/interactions**: mutable container variables, function addresses and
  calls through function pointers, pointer attributes and non-single pointer kinds
  remain explicitly unsupported (M5.1c/other memory work). General array/aggregate
  loads and mutable comptime memory remain M5.2/M6/M7. Comptime-only objects can be
  referenced/folded, but unsupported runtime object layouts produce hard errors.
  Existing integer/call/defer lowering and literal byte storage remain unchanged;
  four previous NYI error cases are now positive TIR snapshots.

### M5.1a Runtime single pointers and const qualification
- **What**: source `*T`/`*const T` types use structural pointer interning. Address-of
  mutable locals reuses their existing allocation; `p.*` reads/writes integer, bool
  or pointer pointees. Grouped lvalues and `&p.*` work, including nested pointers,
  function arguments/results, conditional/block joins and deferred writes.
  IR_DEREF and IR_REF_DEREF validate source dereferences separately from compiler
  loads. TIR_PTR_CAST represents compatible pointer qualification, emitted as a
  QBE long copy rather than an integer cast with unrelated safety checks.
- **Const rules**: a mutable pointer can become read-only. Nested qualifications
  follow 0.17's in-memory coercion rules: a mutable outer pointee makes inner
  qualifiers invariant. `**u8 -> *const *const u8` is allowed;
  `**u8 -> **const u8` is not. Scalar pointee widening is never pointer covariance.
- **Diagnostics/safety**: writes through const pointers, non-pointer dereferences,
  known undefined non-OPV dereferences (including `&p.*`), pointer-to-noreturn and
  single-pointer ordering are rejected. Void/u0 dereferences and stores require no
  memory access, including through known undefined pointers, as in the reference.
  Runtime pointee arithmetic/casts/defer preserve existing checked panic behavior.
- **Tests**: GCC/Clang warning-clean ASan/UBSan suites; 533 unit checks, 526 goldens,
  40 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 157 panic cases. The new generator covers signed/unsigned widths 1–64 with
  load/store, nested const arguments, pointer reassignment and defer snapshots.
  A CI differential compares all 84 qualifier pairs at pointer depths 1–3 in both
  runtime and typed-undefined form (168 cases). Typed undefined no longer bypasses
  const/pointer compatibility or implicitly becomes an integer. Error fixtures
  compare rejection/acceptance with actual 0.17, not a guessed model. Optimized GCC,
  four 60-second fuzz targets and standalone 20,000-mutation replays per stage pass.
- **Limitations**: M5.1 remains partial. Addresses of constants, parameters, globals
  and temporary values explicitly fail until declaration-backed storage/lifetime
  handling in M5.1b; no lazy branch-local spills or static temporary hacks.
  Pointer equality is explicitly unsupported until identity handling; `<` ordering
  of single pointers is a language error. Pointer attributes, non-single sizes,
  aggregate/array dereference and general comptime memory remain later work.
- **Interactions**: alloc/load/store still use existing QBE stack slots and long
  pointer ABI; address taking introduces no new allocator or global storage.
  Dereference result types feed existing assignment coercions, and pointer peers
  choose a compatible existing type without inventing an unsafe nested conversion.
  M4.5b literal byte pointers still support `&text.*` identity, but loading whole
  literal arrays awaits M5.2. General pointer casts remain M5.5.

### M4.5b Explicit panic and first-class literal byte values
- **What**: ordinary quoted/multiline string expressions lower to IR_STR, typed
  as pointers to immutable zero-sentinel u8 arrays. Escapes decode to bytes;
  Unicode escapes encode valid scalar values as UTF-8. VALUE_BYTES interns owned,
  length-aware bytes (including embedded NULs), independent of AST lifetime.
  Aliases, known-condition selection, block exits, local pointer variables and
  same-length runtime branch joins flow through the usual expression/value pipeline.
- **Panic**: @panic takes a normal operand and validates the byte-array-to-const-byte-
  slice conversion. Known array lengths lower directly as pointer/length operands
  to TIR_PANIC and an outlined libc write/abort helper. Literal storage is emitted
  once per referenced ValueId in ELF read-only .rodata, including its zero sentinel.
  Panic writes exactly the message bytes plus newline; it never unwinds defer.
  Comptime panic reports "encountered @panic at comptime: ...". Nested panic and
  syntactically unreachable arguments terminate before the outer panic, as in 0.17.
- **Tests**: warning-clean GCC/Clang ASan/UBSan: 533 unit checks, 500 goldens,
  36 ordinary/reference executable inputs plus one szc-only input, 12 driver checks
  and 154 panic cases. Optimized GCC is clean. Sixteen new panic cases compare
  Unicode boundaries, all 256 byte values, NULs, raw UTF-8, empty/escaped/multiline
  messages, aliases, variables, both runtime choices and cleanup suppression.
  Unit checks exercise byte interning ownership/deduplication/rehashing and binary
  diagnostic storage/printing. Readelf confirms literal .rodata has no write flag.
  Four 60-second fuzz targets and standalone 20,000-mutation replays pass.
- **Reference**: checked actual 0.17 string_literal.parseWrite/parseEscapeSequence,
  AstGen.strLitNodeAsString/simpleUnOp/panic and Sema.zirPanic, plus reference
  execution and diagnostics. Runtime binary messages match byte-for-byte. The
  official diagnostic renderer truncates a comptime panic message at embedded NUL;
  szc intentionally preserves the complete diagnostic bytes and records this difference.
- **Limitations**: general arrays/indexing/slices/string operations remain Phase 5.
  Runtime choices between different-sized literal arrays require slice joins and
  are explicitly unsupported, not falsely labeled incompatible source types.
  Runtime undefined panic slices are explicitly unsupported (reference accepts
  that undefined program). User panic handlers, stack traces, safety modes and
  retries for failed/partial writes remain absent. Quoted identifier canonicalization
  is unchanged; decoding literals does not automatically normalize identifier names.
- **Interactions**: IR_PANIC stores the operand and its argument source location;
  there is no panic-specific literal AST shortcut. TYPE_ARRAY carries element,
  length and zero-sentinel layout. Codegen materializes VALUE_BYTES as read-only
  global addresses and accepts ordinary pointer storage/joins. Diagnostic now stores
  a byte length; diag_report_bytes allows embedded-zero comptime messages, while
  existing formatted diagnostics use the same collector unchanged. Slice ABI and
  more general pointer/aggregate coercions can extend this foundation in Phase 5.

### M4.5a Safe unreachable and noreturn checks
- **What**: explicit source unreachable now uses the existing outlined safety
  runtime, printing "reached unreachable code" and aborting, rather than executing
  raw hlt. Constant-evaluation unreachable remains a compilation error. A distinct
  TIR_TRAP terminates the backend after statically known noreturn calls and for
  internal fallback bodies; it does not claim a source safety violation.
- **Diagnostics**: explicit value/void/undefined returns from noreturn functions
  report "function declared 'noreturn' returns". Implicit return points at the
  signature type. Ordinary casts (including nested result-location coercions)
  report "cannot cast to noreturn". A syntactically noreturn return operand,
  including direct unreachable, is unreachable-code error, like AstGen.reachableExpr.
- **Tests**: warning-clean GCC/Clang ASan/UBSan: 518 unit checks, 479 goldens,
  35 ordinary/reference execution inputs plus one szc-only input, 12 driver checks,
  138 panic cases. Optimized GCC builds cleanly. Generated noreturn joins check all
  128 signed/unsigned 1–64-bit result types, dead panic arms and noreturn cleanup.
  Eight new panic cases cover entry points, calls, branches, loops and cleanup
  suppression. Four 60-second libFuzzer runs and standalone 20,000-mutation replays
  per target pass. Only affected return-IR and short-circuit QBE goldens changed.
- **Reference**: actual 0.17 Sema.zirUnreachable/analyzeAs/coerceExtra, AstGen.ret
  and std.debug.FullPanic.unreach, plus binary execution and diagnostic comparisons.
  Known direct noreturn calls end in backend unreach, not a source-unreachable
  panic; indirect noreturn-returned checks await function-pointer support.
- **Limitations**: explicit @panic remains M4.5b. Its slice-message requirement
  needs proper read-only byte/string value support, not a literal-only AST shortcut.
  General arrays/slices/strings remain Phase 5; user panic overrides, stack traces,
  runtime-safety toggles and other optimization modes are not implemented.
  Comptime function calls and richer coercion diagnostics retain the existing
  Phase 7/aggregate limitations.
- **Interactions**: IR_AS_RETURN distinguishes the outermost direct return-result
  coercion from ordinary IR_AS propagated into branches. Sema uses return-specific
  diagnostics only for the former; return operands are still checked before defer.
  Explicit unreachable alone uses TIR_UNREACHABLE's safety panic. Existing control
  join logic discards noreturn exits, and noreturn cleanup suppresses pending
  transfers. No changes to parser, external QBE invocation or panic unwinding.

### M4.4b Scope-exit defer
- **What**: expression/block defer, reverse registration order, nested cleanup,
  lexical names resolved at registration, and delayed evaluation of cleanup loads
  and call arguments. Defers execute for block fallthrough, implicit void return,
  explicit return, and named/unnamed break/continue. Return/break operands are
  evaluated before cleanup. Continue cleanup precedes the loop increment or
  continue expression; only scopes crossed by the transfer are cleaned up.
- **Control rules**: return from a defer body and break/continue escaping it are
  rejected, including dead conditional paths. Local block/loop control within
  cleanup is allowed. Cleanup can itself be noreturn; subsequent cleanup and the
  pending transfer are then omitted by sema. Panic/unreachable do not unwind defers.
  Non-void discarded cleanup expressions (including cleanup block statements) are
  errors. Errdefer remains an explicit unsupported feature until error unions.
- **Tests**: 518 unit checks, 466 goldens, 33 ordinary/reference executable inputs
  plus one szc-only edge input, 12 driver checks and 130 panic cases. GCC/Clang
  warning-clean sanitizer validation and optimized GCC builds pass. Generated
  snapshots exercise all 128 signed/unsigned 1–64-bit types on both exit paths.
  Stress coverage grows cleanup operand maps beyond their initial capacity;
  panic cases verify each exit kind, cleanup order and operand-before-cleanup.
  Four 60-second fuzz runs, final irgen/sema fuzz rechecks and standalone
  20,000-mutation replays per target pass.
- **Reference**: checked against Zig 0.17 AstGen.deferStmt/genDefers/ret and
  Sema.ensureResultUsed, with execution and diagnostic comparisons using its binary.
- **Limitations**: errdefer/error return traces await Phase 6; mutable comptime
  storage and calls await Phase 7 (constant-only empty cleanup already works).
  Full diagnostic notes and ordinary unused-result/local checks outside cleanup
  remain incomplete, as does quoted identifier canonicalization.
- **Interactions**: arena DeferScope registrations retain one untyped cleanup
  body, while a dedicated defer-body scope blocks escaping control transfers.
  Each scope exit emits IR_DEFER with that reusable body. Sema expands it into
  ordinary typed instructions using a fresh operand-map overlay whose parent
  exposes captured outer refs; cleanup-local operands never leak between exits.
  Map growth preserves its parent. IR_ENSURE_RESULT validates ignored cleanup
  values. Existing TIR/QBE control flow needs no new operation or runtime stack.

### M4.4a Runtime range-based for loops
- **What**: bounded integer ranges, multiple range inputs, unbounded companion
  counters, immutable usize captures/discards, nested loops, labeled break/continue,
  value-producing breaks and exhaustion else. Bounds are evaluated once in source
  order; continue advances the common index and break skips else. Captures do not
  leak into else. Existing result-type propagation and peer joins apply to values.
- **Safety**: endpoints coerce to usize; checked subtraction rejects reversed ranges.
  Known length mismatches report "non-matching for loop lengths". Runtime mismatches
  panic before iteration, including empty inputs, with "for loop over objects with
  non-equal lengths". Unbounded capture addition is checked for overflow.
- **Tests**: 518 unit checks, 449 goldens, 29 ordinary/reference executable inputs
  plus one szc-only edge input, 12 driver checks and 124 panic cases pass with
  warning-clean GCC/Clang ASan/UBSan builds. Optimized GCC also builds cleanly.
  The range generator covers 128 integer types; fixtures exercise endpoint order,
  bound snapshots, labels, else, contextual casts and boundary counters. Four
  60-second libFuzzer runs and standalone 20,000-mutation replays per target pass.
- **Reference**: verified using Zig 0.17.0 AstGen.forExpr, Sema.zirForLen,
  std.debug.forLenMismatch and the official executable, not older semantics.
- **Limitations**: non-range array/slice inputs await Phase 5; inline/comptime for
  awaits Phase 7. Defer is M4.4b; errdefer awaits error unions. Full diagnostic
  notes, unused captures/locals and quoted identifier canonicalization remain
  incomplete. Pointer range captures are invalid in Zig, not a supported future mode.
- **Interactions**: irgen emits bounded lengths through IR_FOR_LEN and lowers the
  loop with existing result/body-exit blocks and explicit repeat targets. Sema
  prefers a known bound and emits TIR_FOR_LEN_CHECK for runtime comparisons;
  codegen uses the existing panic guard infrastructure. No new parser node or
  static control state is introduced. Array/slice for can extend this shared-index
  lowering with memory element access in Phase 5.

### M4.3 Labeled blocks as expressions and named break values
- **What**: labeled block expressions/statements, value/void breaks, nested exits
  to outer blocks and loops, implicit void fallthrough, branch-local results and
  peer-typed joins. Result types propagate from returns, declarations, assignments
  and call arguments into break operands (including integer cast builtins).
  Blocks with no local exit are noreturn. A sole constant exit stays known;
  multiple runtime exits of comptime-only type are rejected like Zig 0.17.0.
- **Comptime**: constant-only block bodies work in container-level declarations,
  including nested named exits, conditional selection, arithmetic, type-valued
  blocks and function aliases. When typed IR is absent, breaks record known
  operands without allocating/emitting TIR; an inner exit to an outer target
  propagates noreturn until that target yields its value.
- **Scoping/diagnostics**: block and loop labels share ControlScope and a label
  redefinition checker. Unnamed jumps skip labeled blocks and find an enclosing
  loop; named continue targeting a block is rejected. Labels are a distinct
  namespace from locals. Unused/missing/redefined labels, fallthrough/type errors,
  comptime-only joins and unreachable break operands are reference-checked.
- **Testing**: IR/TIR/QBE goldens; runtime/comptime/nested-loop examples; all 128
  signed/unsigned 1–64-bit block joins via tools/generate_block_tests.py in CI.
  GCC/Clang sanitizer suites: 518 unit checks, 427 goldens, 25 ordinary e2e inputs
  plus one szc-only edge input, 12 driver checks and 119 panic cases. New safety
  cases exercise checked arithmetic and integer cast guards in break operands;
  ordinary programs and diagnostic messages are checked against actual Zig 0.17.0.
  Four 60-second fuzz runs and standalone corpus replay (20,000 mutations per
  target) are clean, including final irgen/sema replay after updating block seeds.
- **Limitations**: mutable comptime storage, comptime calls/loops and explicit
  comptime expressions remain Phase 7 work. Identifier/label matching still uses
  exact source spelling: quoted labels work with identical spelling, but aliases
  such as foo vs @"foo" and equivalent escapes are not yet normalized. Unused
  locals/results and full secondary diagnostic notes remain unsupported.
- **Interactions**: no new IR/TIR/codegen operation is needed. Source labeled
  blocks use the existing IR_BLOCK/BREAK and deferred BreakExit coercion bodies
  introduced by M4.2. ControlScope has independent flags for unnamed loop jumps
  and continue support. Namespace boundaries still stop label lookup.

### M4.2 Runtime while, break/continue and labeled loops
- **What**: boolean `while`, block/unbraced bodies, continue expressions, nested
  loops, named and unnamed break/continue, loop `else` and value-producing breaks.
  Loop conditions are reevaluated each iteration. Continue branches to a body-exit
  block, then executes the continue expression before repeating; break skips both
  the continue expression and else. Else-side unnamed jumps target an outer loop;
  a named break may target the exhausted loop, but named continue may not.
  Duplicate/unused/missing labels and invalid jump operands are diagnosed.
- **Compound assignments**: checked/wrapping/saturating arithmetic, bitwise and
  shift assignments use load/operation/store. Ordinary shifts provide the RHS's
  Log2Int result type (including @intCast); saturating shifts accept arbitrary
  integer amounts, matching the actual 0.17 AstGen assignment rules.
- **Result joins**: sema tracks explicit control targets. Break operands are
  gathered before peer typing, then coerced in deferred noreturn exit bodies;
  this preserves branch-local operands without inserting a widening cast outside
  its dominating branch. A sole constant exit remains known. Nonreturning loops
  stop sema analysis, and the existing QBE loop/repeat/block/br operations suffice.
- **Testing**: IR/TIR/QBE goldens, reference-checked diagnostics, nested labeled
  loops, early return/break from continue expressions, zero iterations, skipped
  runtime division guards, stack-local reinitialization and typed loop values.
  `tools/generate_loop_tests.py` exercises all 128 signed/unsigned 1–64-bit types,
  included in e2e and CI. Two new panic cases verify checked overflow in a loop
  body and continue expression. GCC/Clang sanitizer suites: 518 unit checks,
  409 goldens, 21 ordinary e2e inputs plus one szc-only edge input, 12 driver
  checks and 117 panic cases; ordinary programs match the official 0.17 binary.
  Four 60-second fuzz runs, final irgen/sema 60-second runs and standalone
  corpus replay are clean.
- **Limitations**: inline/comptime loop evaluation is deferred to Phase 7; even
  a zero-iteration loop in a container-level comptime initializer is not evaluated
  yet. Optional/error-union while captures await M6.3/M6.5. Labeled value blocks
  are M4.3, for/defer M4.4; unused locals/results and full diagnostic notes remain
  unsupported. Arithmetic safety remains always enabled.
- **Interactions**: IR_REPEAT now carries an explicit loop ref. Source while
  lowers to an outer result IR_BLOCK, IR_LOOP, condition IR_CONDBR, and a separate
  body-exit IR_BLOCK for continue. ControlTarget chains are analysis-local (no
  static state); nested IR_IF/boolean branches inherit them. The same block-merge
  infrastructure can be reused by M4.3's labeled blocks.

### M4.1 Source if/else, comparisons and boolean short-circuit
- **What**: source statement/expression `if`, optional `else`, else-if chains,
  unbraced plain assignments, branch-local declarations, integer/bool results,
  result-type propagation into branch casts (return/local/assignment/call argument),
  integer peer-type joins and early returns. Known conditions analyze only the
  selected arm. Runtime arms lower to existing TIR block/condbr/br; a branch that
  returns has no merge operand. A sole constant exit stays comptime-known while
  preserving control effects; two runtime exits reject comptime-only result types,
  even if their values are identical, like Zig 0.17.0.
- **Found on the way**: result coercions must not be appended after direct branch
  terminators. Noreturn result blocks must not emit a dangling QBE join label.
  Known-lhs boolean short-circuiting now propagates a noreturn RHS. Zig accepts
  syntax after an if whose two arms return, then sema skips it; do not add an
  irgen unreachable-code error based on inspecting both arms.
- **Testing**: IR/TIR/QBE goldens and diagnostic cases; executable tests cover
  nested branches, both/one returning arms, void/u0 results, skipped division
  guards, branch assignments, signed/unsigned comparisons and short-circuit
  composition. `tools/generate_conditional_tests.py` adds branch/merge checks for
  all 128 signed/unsigned 1–64-bit types, included in `make e2e-test` and CI.
  GCC/Clang sanitizer suites pass 518 unit checks, 389 goldens, 16 ordinary e2e
  inputs, 12 driver checks and 115 panic cases. Four 60-second libFuzzer runs,
  final sema/codegen 60-second run and standalone corpus replay are clean.
  Diagnostics checked against the actual 0.17.0 binary and Sema.zig's
  zirCondbr/zirBlock implementations. Both runtime arms are analyzed before
  collecting results, so independent branch errors are collected.
- **Limitations**: optional/error-union payload captures are explicitly rejected
  until M6.3/M6.5. Labeled value blocks, loops, break/continue and defer remain
  upcoming milestones. Compound assignments and unused-result/local diagnostics
  remain unsupported. Explicit unreachable still emits hlt until M4.5.
- **Interactions**: IR_IF is a value instruction with an IrCondbr payload and
  break_inline BODY_RESULT arm exits; IR_CONDBR remains a terminator. No new TIR
  operation or backend executable is needed. Noreturn results stop sema body
  analysis; void branches use TIR_REF_NONE and dump without a fake numeric ref.

### M3.6b Runtime saturation and explicit division builtins
- **What**: runtime +|/-|/*| clamp overflow to integer bounds; <<| checks the
  original (possibly u64) amount before narrowing it, handles zero/large shifts,
  and detects truncation by shifting the normalized result back. Added @divTrunc,
  @divFloor, @divCeil, @divExact, @rem and @mod with peer typing and comptime
  evaluation. Signed division guards min/-1; floor/ceil correct truncating results
  according to operand signs and nonzero remainder; exact division guards remainder.
  Modulo follows floor division, including negative denominators as in Zig 0.17.0.
  `comptime.c` owns rounded BigInt quotient/remainder computation, with alias-safe
  output descriptors; zero divisors leave outputs untouched. Known u0 divisors
  and undefined operands are rejected during sema. Matched the reference's special
  max-bound result for nonzero comptime saturating shifts with an amount beyond u64.
  New typed-IR/QBE/error goldens and sema fuzz seeds. Generated all-width tests now
  include saturation and explicit division; panic coverage grows to 115 cases.
- **Validation**: 518 unit checks (including 2,000 randomized BigInt round/alias
  trials), 377 goldens, 12 ordinary e2e inputs, one szc-only edge input, 12 driver
  checks and 115 panic cases pass under GCC/Clang sanitizers. Ordinary e2e and
  panic cases match official Zig 0.17.0; three additional generated seeds also match.
  Four local libFuzzer targets complete 60 seconds each without failures.
- **Limitations / reference discrepancy**: the official x86_64 runtime backend
  traps on @rem/@mod(i64.min, -1), although its comptime evaluator returns zero and
  Sema adds no division-overflow guard for remainder. szc returns zero without
  issuing a trapping hardware division. This deliberate edge difference is covered
  in `tests/run_szc/remainder_minimum.zig`, run with ZIG unset; it is not silently
  included in the ordinary differential suite. Runtime integers wider than 64 bits,
  floats, vectors, optimization-mode safety switches and full panic/std integration
  remain later milestones.
- **Interactions**: builtin names lower through the irgen table to distinct IR/TIR
  operations; codegen shares overflow predicates between checked and saturating
  arithmetic, and emits only needed libc-backed panic helpers. Existing CI
  test/e2e targets include the new goldens, width cases and panic checks.

### M3.6a Checked runtime arithmetic and panic stub
- **What**: typed IR preserves checked vs wrapping add/sub/mul/negation. QBE guards
  detect unsigned carry/borrow and signed overflow after canonical normalization;
  multiplication checks unsigned operand magnitudes against a division threshold,
  including signed minima and zero operands, without relying on wider native integers.
  Checked negation rejects the signed minimum. Integer casts enforce destination
  bounds and signedness, including u0; truncate and wrapping operations remain unchecked.
  Unsigned division/remainder guard zero divisors; shifts guard non-power-of-two
  widths. Panic stubs emit the matching Zig 0.17.0 message to stderr and abort.
  Fixed comptime left shifts to truncate to their fixed width, and reject `i0` as
  Zig does. Added safe boundary regressions, generated checked-op cases over all
  128 integer types, error goldens and runtime panic checks against official Zig.
  The safety harness runs in `make e2e-test` (and independently with `make safety-test`).
  Fresh GCC/Clang `-Werror` sanitizer builds pass 515 unit checks, 369 goldens,
  ten e2e inputs, 12 driver checks and 77 panic cases; reference validation passes
  against official Zig. Standalone fuzz replay is also clean, and all four
  libFuzzer targets completed 60 seconds each without failures.
- **Limitations**: runtime saturation and explicit signed division/remainder builtins
  remain M3.6b; integers wider than 64 bits are still rejected. Runtime checks are
  always enabled: no optimization-mode switch or `@setRuntimeSafety` yet. The panic
  stub has no stack trace, custom panic handler or std integration; explicit
  `unreachable` still uses QBE `hlt` until M4.5. A panic's single stderr write does
  not retry short/interrupted writes.
- **Interactions**: codegen alone lowers safety checks and emits only needed panic
  helpers (libc write/abort through the separate system-cc link). Typed-IR wrapping
  op names differ from checked ops so later optimization modes can preserve semantics.
  The safety harness compiles each source separately, checks SIGABRT and exact szc
  stderr, and checks official Zig's signal/message without depending on stack traces.

### M3.5 End-to-end test harness (`tests/run_e2e.sh`, `tests/run_driver.sh`)
- **What**: `make e2e-test` compiles each `tests/run/*.zig` program with szc,
  verifies its first-line `// exit: N` and optional byte-exact `.stdout` sidecar,
  rejects unexpected stderr,
  checks retained IL/assembly, and exercises `szc run` independently. If `ZIG` is
  set, every source is also compiled and executed by official Zig (compilation
  success is checked separately from program exit). Deterministic generated tests
  cover all 128 signed/unsigned 1–64-bit integer types: wrapping ops, bitwise ops,
  shifts, comparisons, locals and unsigned division/remainder. Driver boundary tests
  check tool overrides/failures, no subprocess after a compile error, paths with
  spaces, argument forwarding, temporary cleanup, invalid CLI and output failures.
  `tools/build_qbe.sh` fetches and builds the pinned official QBE revision.
  CI gcc/clang jobs run native sanitizer e2e tests; the differential job additionally
  checks exit/stdout with official Zig 0.17.0. Existing fuzz schedules remain intact.
  Validation: fresh GCC and Clang `-Werror` ASan/UBSan builds pass all 366
  goldens, nine e2e inputs (including the generated 128-type suite) and 12 driver
  boundary tests. Negative harness checks reject compile failures and stdout
  mismatches. The QBE clone/build script succeeds using the canonical Git transport.
  The new GitHub CI configuration is pushed; check Actions for the latest status.
- **Limitations**: current programs produce no stdout (string/runtime output support
  awaits M5); sidecars support future output tests. Only successful programs with
  deterministic exits are covered; panic tests arrive in M3.6. Generated tests use
  one deterministic seed by default (the generator accepts `--seed`). Ten-second
  execution timeouts prevent a bad compiled program from hanging the suite.
- **Interactions**: `make test` stays independent of QBE; `make e2e-test` requires
  QBE on PATH or `SZC_QBE`. Reference validation is optional locally, enabled in CI
  by `ZIG`. Generated source stays in ignored `build/`, not in the checked-in corpus.

### M3.4 QBE codegen (`src/codegen.[ch]`, `src/driver.[ch]`)
- **What**: typed IR to QBE IL, integer/bool functions and direct calls, arithmetic,
  comparisons, casts, local alloc/load/store, branch/block/loop instructions and `hlt`.
  Integers up to 32 bits use `w`, up to 64 use `l`; narrow integers stay sign- or
  zero-extended after operations. Stack slots are hoisted to entry. A C `main` wrapper
  handles Zig `main` returning u8/void/noreturn. `emit-qbe`, `build-exe` (`-o`,
  `--keep-temps`) and `run` commands; driver executes separate QBE and system cc
  processes without a shell (`SZC_QBE`/`SZC_CC`). Integer builtins `@as`, `@intCast`,
  `@truncate`, `@intFromBool` support runtime lowering and comptime evaluation.
  Checked Zig 0.17.0 `Sema.zig`: truncate permits widening and requires matching
  signedness for typed operands. Fixed runtime `or` branch ordering and nested-body
  local lookup. Golden QBE/cast tests; sema fuzzer also emits QBE after successful
  analysis; eight executable tests compared against the official Zig binary.
  GCC and Clang `-Werror` sanitizer suites pass (366 goldens), all four fuzzers
  complete 60 seconds each, and a generated 128-function integer-width check
  (signed/unsigned 1–64 bits) exits zero with both szc and Zig.
- **Limitations**: runtime integers wider than 64 bits and floats are rejected;
  arithmetic and integer-cast safety checks are deferred to M3.6. Full C ABI handling,
  globals, indirect calls and std startup are not implemented. Driver intermediates
  are named `<output>.ssa`/`.s`; concurrent builds must use different output paths.
  Source-level loops/if still await Phase 4; short-circuit bool control flow works.
- **Interactions**: codegen consumes analysed `SemaFunc`/`TirFunc` and interned values;
  `br` assigns the target block result before jumping. `run` uses a temporary directory
  and propagates program exit status; a failed compiler stage never invokes QBE/cc.
  M3.5 will automate executable exit/stdout and reference-compiler checks.

### M3.3 Typed IR + minimal sema (`src/tir.h`, `src/sema.[ch]`, `src/comptime.[ch]`)
- **What**: typed IR (`tir.h`): per-function instruction array, every instruction typed,
  bodies as in the untyped IR, comptime-known operands as `const` instructions holding a
  `ValueId`. `comptime.c`: arbitrary-precision `BigInt` (add/sub/mul/truncating divmod/shifts/
  two's-complement bitwise ops/wrap/fits/decimal) and an interned value table (void, bool,
  int, type, function, undefined). `sema.c`: interned structural types (builtin types have the
  TypeId of their well-known IR ref; structs are nominal), namespaces from `struct_decl`,
  lazy declaration resolution with dependency-loop detection, function signatures (`func`
  instruction), function bodies analysed from a queue starting at `pub fn main` (and exported
  functions), instruction map from IR instruction to operand (comptime value or runtime typed
  instruction). Operations with comptime-known operands are folded (overflow and division by
  zero are compile errors; wrapping ops wrap; saturating ops clamp); comptime_int coerces to
  integer types with range checks, runtime integers widen with `int_cast`; peer types for
  integer binary ops; comparisons against out-of-range constants are folded; signed `/`/`%`
  errors like Zig; shift amounts coerce to `Log2Int`; `and`/`or` become `block`+`condbr`
  only when the left side is runtime-known; noreturn calls end the block. `szc tir`
  dumper, goldens `tests/tir/*.tir` and `tests/tir/errors/*.err`, BigInt unit tests against
  128-bit arithmetic, `fuzz_sema` target (parse → irgen → sema → dump).
- **Limitations**: no generic functions, comptime calls, comptime variables, container-level
  variables, struct fields, callconv, floats, pointers other than local `alloc`s, function
  pointers (all report "not supported by szc yet"). Runtime arithmetic has no safety checks
  yet (M3.6). Declaration lookup scans the namespace linearly. Error locations of a few
  errors differ from Zig (e.g. implicit return is reported at the closing brace).
- **Interactions**: codegen reads `SemaFunc` (symbol, type, `tir`) for every function in
  state `FUNC_ANALYSED` (or extern + queued) and `sema_type_size/align`; `TIR_REF_NONE` is
  the operand of a `ret` in a `void` function; `br` targets a `block` instruction;
  `condbr` payload is `TirCondbr`; `call` payload is `TirCall` + argument refs. Zig's `main`
  gets an internal symbol (`szc.main.N`); codegen must emit the C `main` wrapper.

### M3.1–M3.2 Untyped IR + irgen basics (`src/ir.h`, `src/irgen.c`, `src/intern.[ch]`)
- **What**: IR design in `docs/IR.md`: per-file instruction array + `extra` words, refs with
  well-known constants (primitive types and values), bodies as instruction lists ending in a
  terminator, payload structs read with `IR_READ` (memcpy, no aliasing). String interner
  (`StringId`). irgen handles structs (fields with type/default bodies; const/var/fn/test/
  comptime declarations with lazily analysed value bodies; duplicate-declaration check),
  functions (param/`anytype` instructions with type bodies that see earlier params, return
  type/callconv bodies, body with `ret_implicit`), integer literals (decoded, big ones as
  32-bit limbs, underscore/leading-zero/digit validation), identifiers (locals, params,
  declarations, primitives, `iN`/`uN`), arithmetic/bitwise/comparison/unary operators,
  short-circuit `and`/`or`, calls with per-argument bodies whose result type is `arg_type`,
  `return` with `ret_type` result type, local `const`/`var` (alloc), plain assignment through
  `elem_type` result type, `_ = x`, unreachable-code and shadowing errors. `szc ir` dumper,
  IR golden tests (`tests/ir/`), `fuzz_irgen` target; irgen runs crash-free (ASan) on all
  1373 Zig sources (reporting unsupported constructs).
- **Limitations**: everything else reports "not supported by szc yet" (control flow, builtins,
  pointers, strings, floats, containers other than auto-layout structs, compound
  assignment, labeled blocks, …). Captures of outer function locals by nested containers are
  not implemented (M7.3). Unused-local errors are not reported.
- **Interactions**: sema walks bodies via `ir_body_count/ir_body_insts`; a declaration's value
  body ends in `break_inline result, value`; `arg_type %call, i` is resolved while sema
  analyses the call's arguments; `elem_type`/`ret_type` give assignment/return result types.

### M2.1–M2.13 AST + parser (`src/ast.[ch]`, `src/parser.[ch]`)
- **What**: one struct per AST node kind (`AstNode` header with kind, file, main-token offset
  and `start`/`end` byte span), `ast_visit_children`, indented dumper (`szc ast`), span
  dumper (`szc ast-spans`). Recursive-descent parser, one function per grammar rule with the
  PEG rule in its comment; binary operators use one Pratt loop over an operator table
  (`parse_expr_precedence`), comparisons are non-associative, operator whitespace symmetry
  and `&&` are checked like the reference. 4-token lookahead ring buffer. Doc comments are
  accepted only where the reference accepts them (before members, error-set names and
  params; `//!` at the start of a container) with the same errors (same-line, unattached,
  on tests/comptime blocks); declarations between fields are reported.
  Error recovery: setjmp/longjmp at member and statement level, skipping like
  `findNextContainerMember`; a stray `}` at the root ends the parse like the reference.
  Nesting limit 1024.
- **Testing**: parses all 1373 `.zig` files of Zig 0.17.0 (`lib/`, `src/`, `test/behavior`,
  `test/standalone`) without errors; node spans of every file equal those of `std.zig.Ast`
  (`tools/ast_spans.zig` + `tools/diff_ast_spans.sh`); accept/reject agrees with the reference
  on >10k mutated sources (`tools/diff_parse_errors.py`); golden tests `tests/parser/*.ast`
  and `tests/parser/errors/*.err`; libFuzzer + standalone fuzzing clean.
- **Found on the way**: the lexer had been ported from an older tokenizer; fixed to 0.17.0
  (no `**` operator, no `.**` token, control characters invalid after `\\` in strings) and the
  reference tests re-ported (336 cases). 0.17.0 also has no `errdefer |e|` payload, and local
  `const`/`var` declarations require an initializer.
- **Limitations**: doc comment text is not stored. Enum fields and untyped union fields are
  stored as tuple-like fields whose `type` is the name identifier (`has_name == false`, like
  the reference AST); irgen decides by container kind. Literals are kept as source spans
  (decoded later). Diagnostic wording is close to, not identical with, the reference, and
  recovery may report a different number of follow-up errors.
- **Interactions**: irgen walks the tree via the node structs (`AST_CAST` checks the kind).
  `start`/`end` spans are only used for tests and diagnostics; `offset` is the main token
  (operator, keyword, name) for error positions.

### M1.1–M1.6 Lexer (`src/lexer.c`, `src/lexer.h`)
- **What**: on-demand state-machine tokenizer matching Zig 0.17.0 `std.zig.Tokenizer`
  token for token: same kinds (`TK_*`, named after `Token.Tag`), boundaries, BOM skip,
  and recovery after an invalid token (resume at the next newline). Keywords are looked up by
  first letter + length (no strcmp scan). `szc tokens` / `szc tokens-kinds` dump commands.
  All `testTokenize` cases (336) from the 0.17.0 reference are ported (`tools/port_tokenizer_tests.py`
  → `tests/lexer/ref/`) and pass. Unit tests: every fixed lexeme round-trips, all 46 keywords,
  EOF is sticky, embedded NUL, BOM. Fuzz target checks ordering/progress/bounds properties;
  2.3M libFuzzer runs and the standalone mutator run clean.
- **Limitations**: number literals are lexed loosely like the reference (e.g. `0xg` is one
  token); validating and decoding them is done at M3.x. String/char escapes are
  not validated or decoded yet. UTF-8 is not validated (same as the reference tokenizer).
- **Interactions**: the parser calls `lexer_next()` on demand and keeps its own lookahead
  buffer. Doc-comment tokens (`TK_DOC_COMMENT`, `TK_CONTAINER_DOC_COMMENT`) reach the
  parser, which must accept them where the grammar allows them. Token text is
  `source[start..end]` and is never copied. Diagnostics take the byte offset (`Token.start`).

### M0.1–M0.5 Infrastructure
- **What**: Makefile (debug/release/asan/test/fuzz), `.editorconfig` (tabs, width 2),
  arena allocator (chunked, overflow-checked, ASan poisoning + red zones), diagnostics
  collector (file:line:col + caret, keeps going), source table (NUL-terminated buffers),
  GitHub Actions CI (gcc+clang `-Werror`, ASan/UBSan tests, 60s libFuzzer per push,
  30 min nightly).
- **Limitations**: diagnostics print in the order they were reported (not sorted). Line/column
  lookup binary-searches a per-file line-start table (added in M2 after a slow error flood).
- **Interactions**: every later stage allocates from an `Arena` and reports through `Diag`.
