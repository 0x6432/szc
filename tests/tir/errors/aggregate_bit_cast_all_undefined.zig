pub fn main() void { const n:u16=@bitCast(@as([2]u8,.{undefined,undefined})); const check:u8=if(n==0)0 else 256; _=check; }
