export fn probe() void { for ([_]u8{1,2}) |x| { @as(u8,x); } }
pub fn main() void {}
