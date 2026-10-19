// nc-lane: reject
// nc-primary: encountered @panic at comptime: enum namespace before layout
const E=enum(u8){a=300,comptime{@panic("enum namespace before layout");}};pub fn main()void{_=E;}
