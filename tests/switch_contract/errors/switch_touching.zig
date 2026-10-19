// switch-lane: error
// switch-families: 12
export fn probe(x:u8)u8{return switch(x){1...3=>1,3...5=>2,else=>0};}

pub fn main()void{}
