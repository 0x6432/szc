// nc-lane: reject
// nc-primary: encountered @panic at comptime: break cleanup
comptime {out:{defer @panic("break cleanup"); break :out;}}
pub fn main() void {}
