const E=enum{};
export fn probe(p:*const E)noreturn{switch(p.*){.missing=>unreachable}}
pub fn main()void{}
