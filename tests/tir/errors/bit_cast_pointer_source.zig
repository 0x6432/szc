pub fn main() void { const n: usize = @bitCast(@as(*u8, @ptrFromInt(16))); _ = n; }
