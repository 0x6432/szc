comptime { const n=@errorName(error{A}); _=n; }
pub fn main() void {}
