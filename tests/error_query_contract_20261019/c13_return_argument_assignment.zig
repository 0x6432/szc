const E=error{A,B};
fn value() E { return @errorCast(error.A); }
fn consume(x:E) bool { return x==error.A; }
export fn probe() bool { var x:E=error.B; x=@errorCast(error.A); return consume(@errorCast(value())) and x==error.A; }
pub fn main() void {}
