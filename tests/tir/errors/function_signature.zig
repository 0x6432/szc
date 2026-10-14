fn f(n:u16)u8{return @intCast(n);}pub fn main()void{const p:*const fn(u8)u8=&f;_=p;}
