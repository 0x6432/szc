// switch-lane: error
// switch-families: 19
export fn probe(x: u8) u8 { return switch (x) { 3 => |v| 1, else => 2, }; }

pub fn main()void{}
