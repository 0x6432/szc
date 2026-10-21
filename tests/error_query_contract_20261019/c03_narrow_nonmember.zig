const Big=error{A,B}; const Small=error{A};
comptime { const x:Big=error.B; const y:Small=@errorCast(x); if(y==error.A) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
