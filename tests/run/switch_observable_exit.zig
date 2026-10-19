// exit: 23
// switch-families: 1,22
fn f(x:u8)u8{return switch(x){0=>return 11,1=>return 23,else=>return 37};}
pub fn main()u8{return f(1);}
