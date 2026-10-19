// switch-lane: error
// switch-families: 14
export fn probe(x: u64) u8 { return switch (x) { 18446744073709551616=>1,else=>2, }; }

pub fn main()void{}
