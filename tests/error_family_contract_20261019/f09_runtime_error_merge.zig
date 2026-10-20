export fn probe(flag:bool)bool{const x=if(flag)error.A else error.B;return x==error.A;}
export fn one_survivor(flag:bool)bool{const x=if(flag)error.A else return false;return x==error.A;}
pub fn main()void{}
