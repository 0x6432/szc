pub fn main() void { const a = [2]u8{1,2}; for (a) |*item| { _ = item; } }
