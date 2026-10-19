// nc-lane: accept
const outer:u8=5;const N=struct{const inner:u8=4;comptime{if(outer+inner!=9) @panic("wrong lexical");}};pub fn main()void{_=N;}
