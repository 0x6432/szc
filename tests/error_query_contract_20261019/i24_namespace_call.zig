const N=struct { fn value() u8 { return 1; } };
export fn probe() void { N.value(); }
pub fn main() void {}
