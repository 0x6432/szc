const E=error{Alpha,Beta};
comptime { const x:E=error.Alpha; const n=@errorName(x); if(n.len!=5 or n[0]!=65) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
