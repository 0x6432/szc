fn copy(p: *volatile [2]u8) void { const a = p.*; _ = a; }
pub fn main() void { var a = [_]u8{1, 2}; copy(&a); }
