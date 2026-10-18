const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(s:S)S{return s;}const H=struct{cb:*const fn(S)S,};const h:H=.{.cb=&f};pub fn main()void{_=h.cb(.{.a=seed(1),.b=2});}
