// switch-lane: error
// switch-families: 14
export fn probe(x: u8) u8 { return switch (x) { -1=>1,else=>2, }; }

pub fn main()void{}
