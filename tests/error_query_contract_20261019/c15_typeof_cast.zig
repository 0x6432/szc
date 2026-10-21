const E=error{A,B};
comptime { const x:E=@errorCast(error.A); if(@TypeOf(x)!=E or x!=error.A) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
