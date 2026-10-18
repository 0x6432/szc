const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}pub fn main()void{const s=f(.{.a=seed(1),.b=2});_=s.a;}
