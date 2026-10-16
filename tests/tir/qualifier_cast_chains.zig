// exit: 33
fn clear_const(p: *const u8) *u8 {
	return @constCast(@as(*const u8, @constCast(p)));
}
fn clear_volatile(p: *volatile u8) *u8 {
	const q: *volatile u8 = @volatileCast(p);
	return @volatileCast(q);
}
fn clear_both(p: *const volatile u8) *u8 {
	return (@constCast((@volatileCast(p))));
}
pub fn main() u8 {
	var value: u8 = 7;
	clear_const(&value).* += 3;
	clear_volatile(&value).* += 5;
	clear_both(&value).* += 18;
	return value;
}
