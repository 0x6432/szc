#!/usr/bin/env python3
"""Port testTokenize() cases from Zig's lib/std/zig/tokenizer.zig into golden tests.

usage: tools/port_tokenizer_tests.py tools/reference/tokenizer.zig tests/lexer/ref

Each case becomes NNNN.zig (exact input bytes) and NNNN.kinds (expected kinds,
then TK_EOF), which tests/run_golden.sh checks with `szc tokens-kinds`.
"""
import os
import re
import sys


def decode_zig_string(text, pos):
	"""Decode a Zig "..." literal starting at text[pos] == '"'. Returns (bytes, end)."""
	assert text[pos] == '"'
	pos += 1
	out = bytearray()
	while text[pos] != '"':
		c = text[pos]
		if c == '\\':
			e = text[pos + 1]
			if e == 'n':
				out += b'\n'
				pos += 2
			elif e == 'r':
				out += b'\r'
				pos += 2
			elif e == 't':
				out += b'\t'
				pos += 2
			elif e in '\\\'"':
				out += e.encode()
				pos += 2
			elif e == 'x':
				out.append(int(text[pos + 2:pos + 4], 16))
				pos += 4
			elif e == 'u':
				close = text.index('}', pos)
				out += chr(int(text[pos + 3:close], 16)).encode('utf-8')
				pos = close + 1
			else:
				raise ValueError('unknown escape \\' + e)
		else:
			out += c.encode('utf-8')
			pos += 1
	return bytes(out), pos + 1


def decode_multiline(text, pos):
	"""Decode consecutive `\\\\` lines starting at pos. Returns (bytes, end)."""
	lines = []
	while True:
		m = re.compile(r'[ \t]*\\\\([^\n]*)\n').match(text, pos)
		if not m:
			break
		lines.append(m.group(1))
		pos = m.end()
	return '\n'.join(lines).encode('utf-8'), pos


def main():
	source_path, out_dir = sys.argv[1], sys.argv[2]
	text = open(source_path, encoding='utf-8').read()
	os.makedirs(out_dir, exist_ok=True)
	count = 0
	skipped = 0
	for m in re.finditer(r'try testTokenize\(', text):
		pos = m.end()
		while text[pos] in ' \t\n':
			pos += 1
		try:
			if text[pos] == '"':
				data, pos = decode_zig_string(text, pos)
			elif text.startswith('\\\\', pos):
				data, pos = decode_multiline(text, pos)
			else:
				skipped += 1
				continue
		except (ValueError, IndexError):
			skipped += 1
			continue
		end = text.index(');', pos)
		rest = text[pos:end]
		lm = re.search(r'&\.\{(.*)\}', rest, re.S)
		if not lm:
			skipped += 1
			continue
		tags = re.findall(r'\.([a-z_0-9]+)', lm.group(1))
		kinds = []
		for tag in tags:
			if tag.startswith('keyword_'):
				tag = tag[len('keyword_'):]
			kinds.append('TK_' + tag.upper())
		kinds.append('TK_EOF')
		name = '%04d' % count
		with open(os.path.join(out_dir, name + '.zig'), 'wb') as f:
			f.write(data)
		with open(os.path.join(out_dir, name + '.kinds'), 'w') as f:
			f.write('\n'.join(kinds) + '\n')
		count += 1
	print('ported %d cases, skipped %d' % (count, skipped))


main()
