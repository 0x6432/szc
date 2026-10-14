fn f()void{}fn invoke(p:*const fn()void)void{p.*();}pub fn main()void{invoke(&f);}
