fn v() void {}
export fn probe() void { defer v(); defer { _ = @as(u8,1); } }
pub fn main() void {}
