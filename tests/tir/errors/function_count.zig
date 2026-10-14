fn f(n:u8)u8{return n;}fn invoke(p:*const fn(u8)u8)u8{return p();}pub fn main()u8{return invoke(&f);}
