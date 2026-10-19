// nc-lane: reject
// nc-primary: encountered @panic at comptime: normal cleanup
comptime {defer @panic("normal cleanup");}
pub fn main() void {}
