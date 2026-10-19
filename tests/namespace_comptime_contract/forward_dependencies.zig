// nc-lane: accept
comptime {if(a!=9) @panic("forward deps");} const a:u8=b+4; const b:u8=5;
pub fn main() void {}
