#!/bin/sh
# Driver boundary checks: errors, tool selection, quoting and cleanup.
set -u
SZC=${1:-build/asan/szc}
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
pass=0
fail=0

check_status() {
	expected=$1
	shift
	"$@" > "$tmp/stdout" 2> "$tmp/stderr"
	actual=$?
	if [ "$actual" -eq "$expected" ]; then
		pass=$((pass + 1))
	else
		echo "FAIL (exit $actual, expected $expected): $*"
		cat "$tmp/stderr"
		fail=$((fail + 1))
	fi
}

cp tests/run/return_value.zig "$tmp/source file.zig"
check_status 0 "$SZC" build-exe "$tmp/source file.zig" -o "$tmp/program with spaces"
if [ -e "$tmp/program with spaces.ssa" ] || [ -e "$tmp/program with spaces.s" ]; then
	echo 'FAIL (intermediate cleanup)'
	fail=$((fail + 1))
fi
check_status 42 "$tmp/program with spaces"
check_status 42 "$SZC" run "$tmp/source file.zig" -- argument 'two words'
check_status 2 env SZC_QBE=/bin/false "$SZC" build-exe tests/run/void_main.zig -o "$tmp/failed"
check_status 2 env SZC_CC=/bin/false "$SZC" build-exe tests/run/void_main.zig -o "$tmp/failed"
check_status 2 env SZC_QBE="$tmp/missing" "$SZC" build-exe tests/run/void_main.zig -o "$tmp/failed"
if [ -e "$tmp/failed.ssa" ] || [ -e "$tmp/failed.s" ]; then
	echo 'FAIL (failed-build cleanup)'
	fail=$((fail + 1))
fi
cat > "$tmp/mark-tool" <<EOF_TOOL
#!/bin/sh
: > "$tmp/tool-invoked"
exit 97
EOF_TOOL
chmod +x "$tmp/mark-tool"
check_status 1 env SZC_QBE="$tmp/mark-tool" SZC_CC="$tmp/mark-tool" "$SZC" build-exe tests/tir/errors/cast_range.zig -o "$tmp/invalid"
if [ -e "$tmp/tool-invoked" ]; then
	echo 'FAIL (external tool invoked after compile error)'
	fail=$((fail + 1))
fi
check_status 1 "$SZC" emit-qbe "$tmp/missing.zig"
check_status 2 "$SZC" build-exe tests/run/void_main.zig -o "$tmp/missing-dir/program"
"$SZC" emit-qbe tests/run/void_main.zig > /dev/full 2> "$tmp/stderr"
actual=$?
if [ "$actual" -eq 2 ]; then
	pass=$((pass + 1))
else
	echo "FAIL (output error exit $actual, expected 2)"
	fail=$((fail + 1))
fi
check_status 2 "$SZC" build-exe tests/run/void_main.zig -o
check_status 2 "$SZC" run tests/run/void_main.zig argument-without-separator

echo "driver: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
