export fn discarded() void { _=@as(error{A},@errorCast(error.A)); }
export fn ignored() void { @as(error{A},@errorCast(error.A)); }
pub fn main() void {}
