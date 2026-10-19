// nc-lane: reject
// nc-primary: encountered @panic at comptime: sizeof activates
const N=struct{comptime{@panic("sizeof activates");}};comptime{_= @sizeOf(N);}
pub fn main() void {}
