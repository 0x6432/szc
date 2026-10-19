// nc-lane: reject
// nc-primary: encountered @panic at comptime: namespace before layout
const N=struct{field:u8=300, comptime{@panic("namespace before layout");}}; pub fn main()void{_=N;}
