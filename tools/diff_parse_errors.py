#!/usr/bin/env python3
"""Accept/reject differential test of the szc parser against std.zig.Ast.

usage: tools/diff_parse_errors.py <szc> <ast_spans-binary> <file-list> [count] [seed]

Makes `count` mutants of randomly chosen files (deleting, duplicating or
inserting a short token-like snippet at a random position), then checks that
szc reports a syntax error exactly when the reference parser does. Mutants
that disagree are kept in build/parse-diff/ for inspection.
"""
import os
import random
import subprocess
import sys

SNIPPETS = [
	"(", ")", "{", "}", "[", "]", ";", ",", ".", ":", "=", "!", "*", "&", "?",
	"|", "-", "+", "..", "...", "=>", ".*", ".?", "!=", "**", "++", "\n", " ",
	"const ", "var ", "fn ", "pub ", "comptime ", "inline ", "if ", "else ",
	"while ", "for ", "switch ", "try ", "catch ", "orelse ", "return ",
	"break ", "continue ", "defer ", "errdefer ", "struct ", "enum ", "union ",
	"align(4) ", "callconv(.c) ", "x", "a: u8", "\"s\"", "'c'", "1", "@foo()",
	"|x|", "|*x|", "-> ", "..=", "\\\\ m\n", "//c\n", "///d\n", "//!d\n",
	"|e| ", "async ", "await ", "usingnamespace ", "linksection(\"s\") ",
	"addrspace(.gs) ", "allowzero ", "volatile ", "anyframe ", "error{} ",
	"noalias ", "anytype ", "export ", "extern ", "threadlocal ", "packed ",
	"opaque {} ", "suspend ", "resume ", "nosuspend ", "asm ", "unreachable ",
	"test {} ", "label: ", ":blk ", "else => ", "inline ", "noinline ",
	"[*c]", "[*]", "[]", "[_]", ":0", "align(4)", "callconv(.c)", ".{}", "{}",
]


def mutate(rng, source):
	pos = rng.randrange(len(source) + 1)
	choice = rng.randrange(3)
	if choice == 0:
		return source[:pos] + source[pos + rng.randrange(1, 4):]
	if choice == 1:
		length = rng.randrange(1, 8)
		return source[:pos] + source[pos:pos + length] + source[pos:]
	return source[:pos] + rng.choice(SNIPPETS) + source[pos:]


def main():
	szc, ref, file_list = sys.argv[1], sys.argv[2], sys.argv[3]
	count = int(sys.argv[4]) if len(sys.argv) > 4 else 500
	rng = random.Random(int(sys.argv[5]) if len(sys.argv) > 5 else 1)
	files = [line.strip() for line in open(file_list) if line.strip()]
	out_dir = "build/parse-diff"
	os.makedirs(out_dir, exist_ok=True)
	paths = []
	for i in range(count):
		source = open(rng.choice(files), "rb").read().decode("utf-8", "replace")
		mutant = source
		for _ in range(rng.randrange(1, 4)):
			mutant = mutate(rng, mutant)
		path = os.path.join(out_dir, "m%05d.zig" % i)
		with open(path, "w", encoding="utf-8") as f:
			f.write(mutant)
		paths.append(path)
	ref_output = subprocess.run([ref] + paths, capture_output=True, text=True).stderr.splitlines()
	ref_error = {}
	current = None
	for line in ref_output:
		if line.startswith("== "):
			current = line[3:]
			ref_error[current] = False
		elif line == "parse error":
			ref_error[current] = True
	disagreements = 0
	for path in paths:
		szc_error = subprocess.run([szc, "parse", path], capture_output=True).returncode != 0
		if szc_error != ref_error[path]:
			disagreements += 1
			print("DISAGREE %s: reference %s, szc %s" % (path, "rejects" if ref_error[path] else "accepts", "rejects" if szc_error else "accepts"))
		else:
			os.remove(path)
	print("parse errors: %d of %d mutants agree" % (count - disagreements, count))
	sys.exit(1 if disagreements else 0)


main()
