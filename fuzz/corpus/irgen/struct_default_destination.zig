const S=struct{a:u8=9,b:u8=8,c:u8=7};
var g:S=.{.a=1,.b=2,.c=3};
noinline fn partial()void{g=.{.b=5,.c=return};}
pub fn main()u8{g=.{.b=g.a};partial();return g.a+g.b+g.c;}
