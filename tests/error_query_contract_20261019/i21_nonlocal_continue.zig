export fn probe() u8 { var i:u8=0; outer: while (i<2) : (i+=1) { _ = { continue :outer; }; } return i; }
pub fn main() void {}
