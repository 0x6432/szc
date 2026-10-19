// nc-lane: reject
// nc-primary: encountered @panic at comptime: enum activated
const E=enum{a,comptime{@panic("enum activated");}};pub fn main()void{_=E;}
