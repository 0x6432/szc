// nc-lane: accept
comptime {const x:u8=out:{{break :out 7;}}; if(x!=7) @panic("bad outer break");}
pub fn main() void {}
