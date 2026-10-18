const S=struct{a:u8,b:u16,}; noinline fn seed(x:u8)u8{return x;} fn f(a:[2][1]S)[2][1]S{return a;}pub fn main()void{_=f(.{.{.{.a=seed(1),.b=2}},.{.{.a=3,.b=4}}});}
