const E=error{@"hello world",B};
comptime { const x:E=@errorCast(error.@"hello world"); if(x!=error.@"hello world") { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
