#!/bin/sh
# Differential test of szc node spans against std.zig.Ast.
# usage: tools/diff_ast_spans.sh <szc> <ast_spans-binary> <file-list>
# Prints the files whose span multisets differ; exit status 1 if any differ.
szc=$1
ref=$2
list=$3
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
failed=0
total=0
while read -r file; do
	total=$((total + 1))
	"$ref" "$file" 2>&1 | tail -n +2 | sort > "$tmp/ref"
	"$szc" ast-spans "$file" 2>/dev/null | sort > "$tmp/szc"
	if ! cmp -s "$tmp/ref" "$tmp/szc"; then
		failed=$((failed + 1))
		echo "MISMATCH $file ($(diff "$tmp/ref" "$tmp/szc" | grep -c '^[<>]') lines)"
	fi
done < "$list"
echo "ast spans: $((total - failed)) of $total files match"
[ "$failed" -eq 0 ]
