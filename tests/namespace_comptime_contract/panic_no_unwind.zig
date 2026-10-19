// nc-lane: reject
// nc-primary: encountered @panic at comptime: body panic
// nc-errors: 1
comptime {defer @panic("unexpected unwind"); @panic("body panic");}
pub fn main() void {}
