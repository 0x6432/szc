const E=error{A,B};
comptime { const x:error{A}=error.A; const y:E=@errorCast(x); if(y!=error.A) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
