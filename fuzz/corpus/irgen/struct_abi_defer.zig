const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(p:*S)S{defer p.a=9;return p.*;}pub fn main()void{var s:S=.{.a=seed(1),.b=2}; _=f(&s);}
