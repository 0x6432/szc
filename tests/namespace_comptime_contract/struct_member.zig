// nc-lane: reject
// nc-primary: encountered @panic at comptime: member activates
const N=struct{pub const x=1; comptime{@panic("member activates");}};pub fn main()void{_=N.x;}
