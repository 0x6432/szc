// nc-lane: accept
const N=struct{comptime{if(@This()!=N) @panic("unpublished self");}};pub fn main()void{_=N;}
