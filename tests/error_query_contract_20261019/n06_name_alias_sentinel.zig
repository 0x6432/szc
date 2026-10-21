const N=struct { const x=error.A; };
comptime { const n=@errorName(N.x); if(n[n.len]!=0) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
