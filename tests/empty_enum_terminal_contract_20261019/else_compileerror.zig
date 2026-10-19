const E=enum{};
export fn probe(p:*const E)noreturn{switch(p.*){else=>@compileError("unreachable empty else")}}
pub fn main()void{}
