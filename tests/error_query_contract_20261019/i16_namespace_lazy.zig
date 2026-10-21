const N = struct { fn bad() void { @as(u8,1); } };
export fn probe() u8 { return 1; }
pub fn main() void {}
