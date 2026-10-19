const E=enum{};
export fn probe(p:*const E,b:bool)void{defer{const x:u8=false;_=x;}if(b){_=p.*;}return;}
pub fn main()void{}
