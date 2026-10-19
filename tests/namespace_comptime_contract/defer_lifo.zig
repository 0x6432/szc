// nc-lane: reject
// nc-primary: encountered @panic at comptime: newer cleanup
comptime {defer @panic("older cleanup"); defer @panic("newer cleanup");}
pub fn main() void {}
