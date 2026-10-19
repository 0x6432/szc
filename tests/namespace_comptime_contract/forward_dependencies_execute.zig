// nc-lane: reject
// nc-primary: encountered @panic at comptime: forward reached
comptime {if(a==9) @panic("forward reached");} const a:u8=b+4; const b:u8=5;
pub fn main() void {}
