# szc build
#
#   make            debug build        -> build/szc
#   make release    optimized build    -> build/szc
#   make asan       ASan+UBSan build   -> build/asan/szc
#   make test       unit + golden tests (uses the ASan build)
#   make fuzz       libFuzzer targets (needs clang) -> build/fuzz/
#   make fuzz-run   run each fuzzer for FUZZ_SECONDS (default 60)

CC         ?= cc
CSTD       ?= -std=c11
WARNINGS   = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
CFLAGS     ?= -O0 -g
ALL_CFLAGS = $(CSTD) $(WARNINGS) $(CFLAGS) $(EXTRA_CFLAGS)
SAN_FLAGS  = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
FUZZ_CC    ?= clang
FUZZ_SECONDS ?= 60

LIB_SOURCES = src/arena.c src/source.c src/diag.c src/intern.c src/lexer.c src/ast.c src/parser.c src/irgen.c src/comptime.c src/sema.c src/codegen.c src/driver.c
HEADERS     = $(wildcard src/*.h)
FUZZERS     = lexer parser irgen sema
GENERATED_RUN_TESTS = build/integer-widths.zig build/conditional-widths.zig build/loop-widths.zig build/block-widths.zig build/for-widths.zig build/defer-widths.zig build/noreturn-widths.zig build/pointer-widths.zig build/global-widths.zig build/function-pointer-widths.zig

.PHONY: all release asan test unit-test golden-test e2e-test safety-test fuzz fuzz-run fuzz-standalone clean

all: build/szc

build/szc: $(LIB_SOURCES) src/main.c $(HEADERS)
	@mkdir -p build
	$(CC) $(ALL_CFLAGS) -o $@ $(LIB_SOURCES) src/main.c

release:
	$(MAKE) CFLAGS="-O2 -g" build/szc

build/asan/szc: $(LIB_SOURCES) src/main.c $(HEADERS)
	@mkdir -p build/asan
	$(CC) $(ALL_CFLAGS) $(SAN_FLAGS) -o $@ $(LIB_SOURCES) src/main.c

asan: build/asan/szc

build/asan/unit_tests: $(LIB_SOURCES) tests/unit/*.c $(HEADERS)
	@mkdir -p build/asan
	$(CC) $(ALL_CFLAGS) $(SAN_FLAGS) -Isrc -o $@ $(LIB_SOURCES) tests/unit/*.c

unit-test: build/asan/unit_tests
	./build/asan/unit_tests

golden-test: build/asan/szc
	./tests/run_golden.sh build/asan/szc

test: unit-test golden-test

build/integer-widths.zig: tools/generate_integer_tests.py
	python3 $< $@

build/conditional-widths.zig: tools/generate_conditional_tests.py
	python3 $< $@

build/loop-widths.zig: tools/generate_loop_tests.py
	python3 $< $@

build/block-widths.zig: tools/generate_block_tests.py
	python3 $< $@

build/for-widths.zig: tools/generate_for_tests.py
	python3 $< $@

build/defer-widths.zig: tools/generate_defer_tests.py
	python3 $< $@

build/noreturn-widths.zig: tools/generate_noreturn_tests.py
	python3 $< $@

build/pointer-widths.zig: tools/generate_pointer_tests.py
	python3 $< $@

build/global-widths.zig: tools/generate_global_tests.py
	python3 $< $@

build/function-pointer-widths.zig: tools/generate_function_pointer_tests.py
	python3 $< $@

# QBE must be on PATH (or SZC_QBE). ZIG enables reference-compiler checks.
e2e-test: build/asan/szc $(GENERATED_RUN_TESTS)
	python3 tests/run_process_timeout.py
	./tests/run_e2e.sh build/asan/szc tests/run/*.zig $(GENERATED_RUN_TESTS)
	env -u ZIG ./tests/run_e2e.sh build/asan/szc tests/run_szc/*.zig
	./tests/run_driver.sh build/asan/szc
	python3 tests/run_reference_storage.py build/asan/szc
	python3 tests/run_function_abi.py build/asan/szc
	python3 tests/run_pointer_alignment.py build/asan/szc
	python3 tests/run_local_alignment.py build/asan/szc
	python3 tests/run_const_alignment.py build/asan/szc
	python3 tests/run_global_alignment.py build/asan/szc
	python3 tests/run_global_const_alignment.py build/asan/szc
	python3 tests/run_safety.py build/asan/szc

safety-test: build/asan/szc
	python3 tests/run_process_timeout.py
	python3 tests/run_safety.py build/asan/szc

# libFuzzer builds (clang)
build/fuzz/fuzz_%: fuzz/fuzz_%.c $(LIB_SOURCES) $(HEADERS)
	@mkdir -p build/fuzz
	$(FUZZ_CC) $(CSTD) -g -O1 -Isrc -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $@ $< $(LIB_SOURCES)

fuzz: $(addprefix build/fuzz/fuzz_,$(FUZZERS))

fuzz-run: fuzz
	@for f in $(FUZZERS); do \
		mkdir -p build/fuzz/corpus_$$f; \
		./build/fuzz/fuzz_$$f -max_total_time=$(FUZZ_SECONDS) -max_len=4096 build/fuzz/corpus_$$f fuzz/corpus/$$f || exit 1; \
	done

# Standalone fuzz drivers (any compiler): replay the corpus and random inputs under ASan.
build/asan/fuzz_%: fuzz/fuzz_%.c fuzz/standalone.c $(LIB_SOURCES) $(HEADERS)
	@mkdir -p build/asan
	$(CC) $(ALL_CFLAGS) $(SAN_FLAGS) -Isrc -o $@ $< fuzz/standalone.c $(LIB_SOURCES)

fuzz-standalone: $(addprefix build/asan/fuzz_,$(FUZZERS))
	@for f in $(FUZZERS); do ./build/asan/fuzz_$$f fuzz/corpus/$$f || exit 1; done

clean:
	rm -rf build
