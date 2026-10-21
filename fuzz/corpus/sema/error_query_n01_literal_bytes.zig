comptime { const n=@errorName(error.Alpha); if(n.len!=5 or n[0]!=65 or n[4]!=97) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
