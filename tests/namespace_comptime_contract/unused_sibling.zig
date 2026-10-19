// nc-lane: accept
const N=struct{const Inner=struct{comptime{@panic("unused sibling");}}; comptime{const x:u8=1;_=x;}};pub fn main()void{_=N;}
