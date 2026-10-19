// switch-lane: error
// switch-families: 24
fn f(x:?u8)u8{return switch(x){null=>0,else=>1};}export fn probe(x:u8)u8{return f(x);}

pub fn main()void{}
