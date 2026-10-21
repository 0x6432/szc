export fn probe(flag: bool) u8 { return blk: { if (flag) break :blk @as(u8,1); break :blk @as(u8,2); }; }
pub fn main() void {}
