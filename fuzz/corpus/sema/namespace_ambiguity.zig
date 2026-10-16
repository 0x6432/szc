const value:u8=1;
const N=struct { const value:u8=2; fn read() u8 { return value; } };
pub fn main() void { _=N.read(); }
