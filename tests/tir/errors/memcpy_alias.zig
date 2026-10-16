var a:[3]u16=.{1,2,3}; pub fn main() void { @memcpy(a[0..2],a[1..3]); }
