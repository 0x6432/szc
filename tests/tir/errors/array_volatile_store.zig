fn copy(p: *volatile [2]u8) void { p.* = [_]u8{1, 2}; }
pub fn main() void { var a = [_]u8{1, 2}; copy(&a); }
