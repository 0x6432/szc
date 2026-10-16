var global:[2][0]type=undefined;
fn identity(a:[2][0]type) [2][0]type { return a; }
pub fn main() void {
	var a:[2][0]type=.{.{},.{}};
	a=identity(a);
	global=a;
}
