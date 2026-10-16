pub fn main() void { @memset(@as(*[1][2]u8,undefined),.{1,2}); }
