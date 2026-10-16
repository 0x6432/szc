pub fn main() void { const p: []const u16 = undefined; const q: [:17]u16 = @constCast(p); _ = q; }
