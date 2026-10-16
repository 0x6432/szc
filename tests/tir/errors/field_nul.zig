const N=struct { const x:u8=7; }; pub fn main() void { _=@field(N,"x\x00suffix"); }
