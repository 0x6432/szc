const E=error{A,B};
const N=struct{const x:E=error.A;};
export fn store(p:*E)void{p.*=error.A;}
export fn address()*const E{return &N.x;}
pub fn main()void{}
