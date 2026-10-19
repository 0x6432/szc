// nc-lane: reject
// nc-primary: encountered @panic at comptime: definition this
const N=struct{const Self=@This();comptime{if(@This()==Self) @panic("definition this");}};pub fn main()void{_=N;}
