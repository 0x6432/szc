const E=error{A,B};
fn name(x:E) bool { return @errorName(x).len!=0; }
export fn probe() bool { return name(error.A); }
pub fn main() void {}
