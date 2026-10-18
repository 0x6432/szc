const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} const T=struct{a:u8,b:u16,};fn f(s:S)u8{return s.a;}pub fn main()void{const t:T=.{.a=seed(1),.b=2};_=f(t);}
