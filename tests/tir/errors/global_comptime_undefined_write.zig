var n: u8 = 1;
const c = blk: { n = undefined; break :blk @as(u8, 0); };
pub fn main() u8 { return c; }
