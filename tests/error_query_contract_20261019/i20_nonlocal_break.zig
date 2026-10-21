export fn probe() u8 { return blk: { _ = { break :blk @as(u8,7); }; }; }
pub fn main() void {}
