#!/bin/sh
# Native and optional reference checks. A test starts with // exit: N.
# <test>.stdout is byte-exact expected stdout; absent means empty stdout.
# Set ZIG to also compile and run every test with the official compiler.
# // outlined-data: true expects a standalone-IL limitation and tests full assembly.
set -u
SZC=${1:-build/asan/szc}
[ "$#" -eq 0 ] || shift
[ "$#" -ne 0 ] || set -- tests/run/*.zig
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
: > "$tmp/empty"
pass=0
fail=0

check_program() {
	label=$1
	expected=$2
	stdout=$3
	shift 3
	timeout 10 "$@" > "$tmp/stdout" 2> "$tmp/stderr"
	actual=$?
	if [ "$actual" -ne "$expected" ]; then
		echo "FAIL ($label exit $actual, expected $expected)"
		cat "$tmp/stderr"
		return 1
	fi
	if [ -s "$tmp/stderr" ]; then
		echo "FAIL ($label unexpected stderr)"
		cat "$tmp/stderr"
		return 1
	fi
	if ! cmp -s "$stdout" "$tmp/stdout"; then
		echo "FAIL ($label stdout)"
		diff -u "$stdout" "$tmp/stdout"
		return 1
	fi
	return 0
}

for input do
	expected=$(sed -n '1s|^// exit: \([0-9][0-9]*\)$|\1|p' "$input")
	case "$expected" in
		''|*[!0-9]*) echo "FAIL (missing exit expectation): $input"; fail=$((fail + 1)); continue ;;
	esac
	if [ "$expected" -gt 255 ]; then
		echo "FAIL (exit expectation exceeds 255): $input"
		fail=$((fail + 1))
		continue
	fi
	stdout=${input%.zig}.stdout
	[ -f "$stdout" ] || stdout=$tmp/empty
	outlined=false
	if grep -q '^// outlined-data: true$' "$input"; then
		outlined=true
		"$SZC" emit-qbe "$input" > "$tmp/expected.ssa" 2> "$tmp/emit-error"
		status=$?
		if [ "$status" -ne 1 ] || ! grep -q 'static alignment above 64 requires assembly output' "$tmp/emit-error"; then
			echo "FAIL (standalone outlined-data diagnostic): $input"
			fail=$((fail + 1))
			continue
		fi
	else
		if ! "$SZC" emit-qbe "$input" > "$tmp/expected.ssa"; then
			echo "FAIL (emit-qbe): $input"
			fail=$((fail + 1))
			continue
		fi
	fi
	if ! "$SZC" build-exe "$input" -o "$tmp/native" --keep-temps; then
		echo "FAIL (build-exe): $input"
		fail=$((fail + 1))
		continue
	fi
	if [ ! -s "$tmp/native.ssa" ] || [ ! -s "$tmp/native.s" ] || { [ "$outlined" = false ] && ! cmp -s "$tmp/expected.ssa" "$tmp/native.ssa"; }; then
		echo "FAIL (retained intermediates): $input"
		fail=$((fail + 1))
		continue
	fi
	if ! check_program "native $input" "$expected" "$stdout" "$tmp/native"; then
		fail=$((fail + 1))
		continue
	fi
	if ! check_program "run $input" "$expected" "$stdout" "$SZC" run "$input"; then
		fail=$((fail + 1))
		continue
	fi
	if [ -n "${ZIG:-}" ]; then
		# Compilation success is checked separately: a compile error must never
		# count as an expected nonzero program exit.
		if ! "$ZIG" build-exe "$input" -femit-bin="$tmp/reference"; then
			echo "FAIL (reference compilation): $input"
			fail=$((fail + 1))
			continue
		fi
		if ! check_program "reference $input" "$expected" "$stdout" "$tmp/reference"; then
			fail=$((fail + 1))
			continue
		fi
	fi
	pass=$((pass + 1))
done

echo "e2e: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
