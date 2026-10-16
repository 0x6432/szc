pub fn main() void { const n:u64=@bitCast(@as([1]*const u8,.{@ptrFromInt(16)})); _=n; }
