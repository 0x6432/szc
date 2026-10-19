// switch-lane: error
// switch-families: 19
export fn probe(x:u8)u8{return switch(x){1=>|v,i| v+i,else=>0};}

pub fn main()void{}
