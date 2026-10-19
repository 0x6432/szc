// switch-lane: error
// switch-families: 18
export fn probe(x: u8) u8 { return switch (x) { 0 => 2, else => |v| blk: { const a: [v]u8 = undefined; _ = &a; break :blk 1; }, }; }

pub fn main()void{}
