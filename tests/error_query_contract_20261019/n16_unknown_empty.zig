comptime { const x:error{}=undefined; const n=@errorName(x); _=n; }
pub fn main() void {}
