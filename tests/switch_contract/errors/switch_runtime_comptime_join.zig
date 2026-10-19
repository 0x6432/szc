// switch-lane: error
// switch-families: 21
export fn probe(x:u8)u8{const r=switch(x){0=>1,1=>2,else=>3};return r;}

pub fn main()void{}
