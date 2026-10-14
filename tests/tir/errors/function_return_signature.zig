fn f(n:u8)u16{return n;}pub fn main()void{const p:*const fn(u8)u8=&f;_=p;}
