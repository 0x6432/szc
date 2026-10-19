// switch-lane: error
// switch-families: 19
export fn probe(x: u8) u8 { return switch (x) { 3...4 => |_| 1, else => 2, }; }

pub fn main()void{}
