comptime { const a=[_]u8{@errorName(error.A)[0],@errorName(error.B)[0]}; if(a[0]!=65 or a[1]!=66) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
