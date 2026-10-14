fn f(n:u8)u8{return n;}fn invoke(p:*const fn(u8)u8)u8{return p(true);}pub fn main()u8{return invoke(&f);}
