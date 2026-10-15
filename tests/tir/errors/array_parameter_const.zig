fn change(a: [2]u8) void { a[0] = 3; }
pub fn main() void { change(.{1, 2}); }
