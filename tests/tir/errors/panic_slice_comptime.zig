const s: []const u8 = "slice message"; const x = @panic(s); pub fn main() void { _ = x; }
