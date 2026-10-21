export fn probe() void { _ = @as(u8,1); _ = u8; _ = "abc"; _ = @as([2]u8,.{1,2}); }
pub fn main() void {}
