// switch-lane: error
// switch-families: 15
export fn probe(x: u8) u8 { return switch (x) { _=>1, }; }

pub fn main()void{}
