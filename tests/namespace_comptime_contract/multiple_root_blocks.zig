// nc-lane: reject
// nc-primary: encountered @panic at comptime: second block
comptime {const x:u8=1;_=x;} comptime {@panic("second block");}
pub fn main() void {}
