threadlocal var a: [2]u8 = .{1,2};
pub fn main() void { _ = &a; }
