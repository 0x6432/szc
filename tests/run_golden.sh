#!/bin/sh
# Golden tests.
#   tests/lexer/**/NAME.zig + NAME.kinds   -> compare `szc tokens-kinds`
#   tests/lexer/**/NAME.zig + NAME.tokens  -> compare `szc tokens` (stdout only)
# usage: tests/run_golden.sh path/to/szc
set -u
export ASAN_OPTIONS="${ASAN_OPTIONS:-exitcode=99}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:exitcode=99:print_stacktrace=1}"
SZC=${1:-build/szc}
pass=0
fail=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

check() {
	# $1 = command, $2 = input, $3 = expected file
	"$SZC" "$1" "$2" >"$tmp" 2>/dev/null
	status=$?
	if [ $status -ge 2 ]; then
		echo "CRASH ($status): $2"
		fail=$((fail + 1))
		return
	fi
	if cmp -s "$tmp" "$3"; then
		pass=$((pass + 1))
	else
		echo "FAIL: $2"
		diff "$3" "$tmp" | head -10
		fail=$((fail + 1))
	fi
}

for input in $(find tests/lexer -name '*.zig' | sort); do
	base=${input%.zig}
	if [ -f "$base.kinds" ]; then
		check tokens-kinds "$input" "$base.kinds"
	fi
	if [ -f "$base.tokens" ]; then
		check tokens "$input" "$base.tokens"
	fi
done

echo "golden: $pass passed, $fail failed"
[ $fail -eq 0 ]
