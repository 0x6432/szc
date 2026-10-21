const Big=error{A,B}; const Small=error{A};
comptime { const y:Small=@errorCast(Big.A); if(y!=error.A or @TypeOf(y)!=Small) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
