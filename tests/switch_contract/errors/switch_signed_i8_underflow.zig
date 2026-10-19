// switch-lane: error
// switch-families: 14
export fn probe(x: i8) u8 { return switch (x) { -129=>1,else=>2, }; }

pub fn main()void{}
