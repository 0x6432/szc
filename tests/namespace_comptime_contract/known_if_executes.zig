// nc-lane: reject
// nc-primary: encountered @panic at comptime: selected if
comptime {if(3==3) @panic("selected if");}
pub fn main() void {}
