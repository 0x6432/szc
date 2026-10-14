fn f(noalias p:*u8)void{_=p;}pub fn main()void{const p:*const fn(*u8)void=&f;_=p;}
