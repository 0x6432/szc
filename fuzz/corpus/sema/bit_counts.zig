// exit: 0
fn check_byte(n: u8) bool {
	var bits = n;
	var ones: u8 = 0;
	var length: u8 = 0;
	while (bits != 0) {
		ones += bits & 1;
		length += 1;
		bits >>= 1;
	}
	bits = n;
	var trailing: u8 = 0;
	if (bits == 0) {
		trailing = 8;
	} else {
		while (bits & 1 == 0) {
			trailing += 1;
			bits >>= 1;
		}
	}
	const signed: i8 = @bitCast(n);
	return @popCount(n) == ones and @clz(n) == 8 - length and @ctz(n) == trailing
		and @popCount(signed) == ones and @clz(signed) == 8 - length and @ctz(signed) == trailing;
}
fn count_u0(n: u0, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u1(n: u1, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i1(n: i1, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u5(n: u5, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i5(n: i5, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u8(n: u8, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i8(n: i8, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u29(n: u29, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i29(n: i29, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u32(n: u32, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i32(n: i32, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u33(n: u33, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i33(n: i33, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u40(n: u40, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i40(n: i40, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u63(n: u63, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i63(n: i63, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u64(n: u64, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i64(n: i64, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
pub fn main() u8 {
	var n: u16 = 0;
	while (n < 256) : (n += 1)
		if (!check_byte(@intCast(n))) return 1;
	if (!count_u0(0, 0, 0, 0)) return 2;
	if (!count_u1(0, 1, 1, 0)) return 2;
	if (!count_u1(1, 0, 0, 1)) return 2;
	if (!count_i1(0, 1, 1, 0)) return 2;
	if (!count_i1(-1, 0, 0, 1)) return 2;
	if (!count_u5(0, 5, 5, 0)) return 2;
	if (!count_u5(1, 4, 0, 1)) return 2;
	if (!count_u5(31, 0, 0, 5)) return 2;
	if (!count_u5(16, 0, 4, 1)) return 2;
	if (!count_i5(0, 5, 5, 0)) return 2;
	if (!count_i5(1, 4, 0, 1)) return 2;
	if (!count_i5(-1, 0, 0, 5)) return 2;
	if (!count_i5(-16, 0, 4, 1)) return 2;
	if (!count_u8(0, 8, 8, 0)) return 2;
	if (!count_u8(1, 7, 0, 1)) return 2;
	if (!count_u8(255, 0, 0, 8)) return 2;
	if (!count_u8(128, 0, 7, 1)) return 2;
	if (!count_i8(0, 8, 8, 0)) return 2;
	if (!count_i8(1, 7, 0, 1)) return 2;
	if (!count_i8(-1, 0, 0, 8)) return 2;
	if (!count_i8(-128, 0, 7, 1)) return 2;
	if (!count_u29(0, 29, 29, 0)) return 2;
	if (!count_u29(1, 28, 0, 1)) return 2;
	if (!count_u29(536870911, 0, 0, 29)) return 2;
	if (!count_u29(268435456, 0, 28, 1)) return 2;
	if (!count_i29(0, 29, 29, 0)) return 2;
	if (!count_i29(1, 28, 0, 1)) return 2;
	if (!count_i29(-1, 0, 0, 29)) return 2;
	if (!count_i29(-268435456, 0, 28, 1)) return 2;
	if (!count_u32(0, 32, 32, 0)) return 2;
	if (!count_u32(1, 31, 0, 1)) return 2;
	if (!count_u32(4294967295, 0, 0, 32)) return 2;
	if (!count_u32(2147483648, 0, 31, 1)) return 2;
	if (!count_i32(0, 32, 32, 0)) return 2;
	if (!count_i32(1, 31, 0, 1)) return 2;
	if (!count_i32(-1, 0, 0, 32)) return 2;
	if (!count_i32(-2147483648, 0, 31, 1)) return 2;
	if (!count_u33(0, 33, 33, 0)) return 2;
	if (!count_u33(1, 32, 0, 1)) return 2;
	if (!count_u33(8589934591, 0, 0, 33)) return 2;
	if (!count_u33(4294967296, 0, 32, 1)) return 2;
	if (!count_i33(0, 33, 33, 0)) return 2;
	if (!count_i33(1, 32, 0, 1)) return 2;
	if (!count_i33(-1, 0, 0, 33)) return 2;
	if (!count_i33(-4294967296, 0, 32, 1)) return 2;
	if (!count_u40(0, 40, 40, 0)) return 2;
	if (!count_u40(1, 39, 0, 1)) return 2;
	if (!count_u40(1099511627775, 0, 0, 40)) return 2;
	if (!count_u40(549755813888, 0, 39, 1)) return 2;
	if (!count_i40(0, 40, 40, 0)) return 2;
	if (!count_i40(1, 39, 0, 1)) return 2;
	if (!count_i40(-1, 0, 0, 40)) return 2;
	if (!count_i40(-549755813888, 0, 39, 1)) return 2;
	if (!count_u63(0, 63, 63, 0)) return 2;
	if (!count_u63(1, 62, 0, 1)) return 2;
	if (!count_u63(9223372036854775807, 0, 0, 63)) return 2;
	if (!count_u63(4611686018427387904, 0, 62, 1)) return 2;
	if (!count_i63(0, 63, 63, 0)) return 2;
	if (!count_i63(1, 62, 0, 1)) return 2;
	if (!count_i63(-1, 0, 0, 63)) return 2;
	if (!count_i63(-4611686018427387904, 0, 62, 1)) return 2;
	if (!count_u64(0, 64, 64, 0)) return 2;
	if (!count_u64(1, 63, 0, 1)) return 2;
	if (!count_u64(18446744073709551615, 0, 0, 64)) return 2;
	if (!count_u64(9223372036854775808, 0, 63, 1)) return 2;
	if (!count_i64(0, 64, 64, 0)) return 2;
	if (!count_i64(1, 63, 0, 1)) return 2;
	if (!count_i64(-1, 0, 0, 64)) return 2;
	if (!count_i64(-9223372036854775808, 0, 63, 1)) return 2;
	return 0;
}
