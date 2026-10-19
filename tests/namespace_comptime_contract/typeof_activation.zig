// nc-lane: reject
// nc-primary: encountered @panic at comptime: typeof activates
const N=struct{comptime{@panic("typeof activates");}}; comptime{_= @TypeOf(N);}
pub fn main() void {}
