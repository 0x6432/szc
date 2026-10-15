const s: []const u8 = &[3]u8{65,0,255}; const x = @panic(s); pub fn main() void { _ = x; }
