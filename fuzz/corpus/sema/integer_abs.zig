// exit: 0
fn magnitude(n:i33) u33 { return @abs(n); }
fn identity(n:u5) u5 { return @abs(n); }
pub fn main() u8 {
	const wide = @abs(@as(i257,-115792089237316195423570985008687907853269984665640564039457584007913129639936));
	const check:u8=if(wide == 115792089237316195423570985008687907853269984665640564039457584007913129639936)0 else 256;
	_ = check;
	const literal = @abs(-12345678901234567890);
	const check_literal:u8=if(literal == 12345678901234567890)0 else 256;
	_ = check_literal;
	if (magnitude(-4294967296) != 4294967296 or identity(31) != 31) return 1;
	return 0;
}
