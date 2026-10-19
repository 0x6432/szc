// switch-lane: error
// switch-families: 13
export fn probe(x: u8) u8 { return switch (x) { 0...255 => 1, else => 2, }; }

pub fn main()void{}
