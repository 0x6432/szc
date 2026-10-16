fn f(p:[][2]u8,v:[2]u8) void { @memset(p,v); }
pub fn main() void {
	var a:[3][2]u8=.{.{1,2},.{3,4},.{5,6}};
	f(&a,.{7,9});
	@memset(&a,a[1]);
}
