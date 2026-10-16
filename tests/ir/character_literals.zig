// exit: 0
fn add(a: u32, b: u32) u32 { return a + b; }
pub fn main() u8 {
	if ('a' != 97 or 'Z' != 90 or '0' != 48) return 1;
	if ('\n' != 10 or '\r' != 13 or '\t' != 9) return 2;
	if ('\\' != 92 or '\'' != 39 or '\"' != 34) return 3;
	if ('\x00' != 0 or '\x7f' != 127 or '\xff' != 255) return 4;
	if ('ä' != 228 or 'ぁ' != 12353 or '😀' != 128512) return 5;
	if ('\u{7ff}' != 2047 or '\u{10ffff}' != 1114111) return 6;
	// Zig 0.17 accepts escaped surrogate code points as integer literals.
	if ('\u{d800}' != 55296 or '\u{dfff}' != 57343) return 7;
	const a = [4]u21{'ä', 'ぁ', '😀', '\u{10ffff}'};
	if (add(a[0], a[1]) != 12581 or add(a[2], a[3]) != 1242623) return 8;
	const s = "\xff\u{7ff}\u{10000}";
	if (s.len != 7 or s[0] != 255 or s[1] != 223 or s[3] != 240) return 9;
	return 0;
}
