const E=error{Alpha,Beta};
comptime { const n=@errorName(E.Beta); if(n.len!=4 or n[0]!=66) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
