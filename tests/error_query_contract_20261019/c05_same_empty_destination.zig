comptime { const x:error{A}=@errorCast(error.A); if(x!=error.A) { const failed_assertion: u8 = false; _ = failed_assertion; } }
fn empty() void { const y:error{}=@errorCast(error.A); if(y==error.A) unreachable; }
export fn probe() void { empty(); }
pub fn main() void {}
