const Big=error{A,B}; const Small=error{A};
comptime { const x:Big=error.A; const y=@as(Small,@errorCast(x)); if(y!=error.A or @TypeOf(y)!=Small) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
