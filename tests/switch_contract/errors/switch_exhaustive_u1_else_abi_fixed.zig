// switch-lane: error
// switch-families: 13
fn inner(x:u1) u8 { return switch(x) { 0=>1,1=>2,else=>3 }; }
export fn probe(x:u8) u8 { return inner(@truncate(x)); }

pub fn main()void{}
