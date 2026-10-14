fn f()void{}fn g()void{}fn choose(c:bool)void{const p=if(c)f else g;p();}pub fn main()void{choose(true);}
