// switch-lane: error
// switch-families: 18
export fn probe(x: u8) u8 { return switch (x) { 3...3 => |v| blk: { const a: [v]u8 = undefined; _ = &a; break :blk 1; }, else => 2, }; }

pub fn main()void{}
