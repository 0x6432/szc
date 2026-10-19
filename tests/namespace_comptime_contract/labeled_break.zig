// nc-lane: accept
comptime {const x:u8=out:{if(true) break :out 7; break :out 9;}; if(x!=7) @panic("bad break");}
pub fn main() void {}
