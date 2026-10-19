// switch-lane: error
// switch-families: 19
export fn probe(x: u8) u8 { return switch (x) { else => |_| 1, }; }

pub fn main()void{}
