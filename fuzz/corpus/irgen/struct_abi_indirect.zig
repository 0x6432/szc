const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(x:u8,s:S,y:u16)S{return .{.a=s.a+x,.b=s.b+y};}pub fn main()void{const p:*const fn(u8,S,u16)S=&f;_=p(seed(1),.{.a=2,.b=3},4);}
