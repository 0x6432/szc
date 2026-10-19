// nc-lane: unsupported
// nc-nyi: comptime variables
comptime {var x:u8=1;x+=1;if(x!=2) @panic("bad mutable");}
pub fn main() void {}
