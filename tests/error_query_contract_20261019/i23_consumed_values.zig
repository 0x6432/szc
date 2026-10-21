fn value() u8 { return 7; }
export fn probe() u8 { const a=[_]u8{value(),2}; return a[0]; }
pub fn main() void {}
