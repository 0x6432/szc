const E=error{A,B};fn pass(x:E)E{return x;}export fn probe(p:*const E)bool{return pass(p.*)==error.A;}
pub fn main()void{}
