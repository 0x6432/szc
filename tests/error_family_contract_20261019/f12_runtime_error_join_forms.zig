export fn branch(flag:bool)bool{const x=if(flag)error.A else error.B;return x==error.A;}
export fn choose(tag:u8)bool{const x=switch(tag){0=>error.A,else=>error.B};return x==error.A;}
export fn labeled(flag:bool)bool{const x=outer:{if(flag)break :outer error.A;break :outer error.B;};return x==error.A;}
pub fn main()void{}
