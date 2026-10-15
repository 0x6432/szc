pub fn main() void { const s: []const u8 = "hi"; const p: *[2]u8 = &s.*; _ = p; }
