const E=error{A,B};
comptime { const x:E=@errorCast(error.A); if(x!=error.A or @TypeOf(x)!=E) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
