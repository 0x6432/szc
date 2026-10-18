const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)u8{s.a=7;return s.a;}pub fn main()void{_=f(.{.a=seed(1),.b=2});}
