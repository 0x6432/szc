const N=struct { fn bad() u8 { return 256; } }; pub fn main() void { _=&@field(N,"bad"); }
