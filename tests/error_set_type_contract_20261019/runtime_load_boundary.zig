const E=error{A,B};export fn probe(p:*const E)bool{return p.*==p.*;}
export fn empty(p:*const error{})bool{return p.*==p.*;}
pub fn main()void{}
