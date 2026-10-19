const E=error{A,B};fn pass(x:E)E{return x;}export fn probe(p:*const E)bool{return pass(p.*)==p.*;}
pub fn main()void{}
