fn f(p:*u8)void{_=p;}pub fn main()void{const p:*const fn(noalias *u8)void=&f;_=p;}
