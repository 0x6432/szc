// switch-lane: error
// switch-families: 24
fn f(x:error{Oops}!u8)u8{return switch(x){else=>1};}export fn probe(x:u8)u8{return f(x);}

pub fn main()void{}
