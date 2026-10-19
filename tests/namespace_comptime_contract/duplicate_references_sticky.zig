// nc-lane: reject
// nc-primary: encountered @panic at comptime: once only
// nc-errors: 1
const N=struct{comptime{@panic("once only");}};pub fn main()void{_=N;_=N;_=N;}
