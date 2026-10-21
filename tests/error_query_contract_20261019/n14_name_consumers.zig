export fn ignored() void { @errorName(error.A); }
export fn discarded() void { _=@errorName(error.A); }
pub fn main() void {}
