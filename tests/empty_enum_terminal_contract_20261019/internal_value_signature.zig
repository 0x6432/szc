const E=enum{};
fn f(x:E)noreturn{switch(x){}}
export fn probe(p:*const E)noreturn{return f(p.*);}
pub fn main()void{}
