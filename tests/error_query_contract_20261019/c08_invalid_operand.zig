comptime { const x:error{A}=@errorCast(@as(u8,1)); if(x==error.A) {} }
pub fn main() void {}
