comptime { const x=@errorCast(error.A); if(x==error.A) {} }
export fn discarded() void { _=@errorCast(error.A); }
pub fn main() void {}
