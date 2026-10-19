// nc-lane: reject
// nc-primary: encountered @panic at comptime: struct activated
const N=struct{comptime{@panic("struct activated");}}; pub fn main()void{_=N;}
