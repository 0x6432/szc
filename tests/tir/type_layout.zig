pub fn main() u8 { const n: u8 = @sizeOf([2]u16) + @alignOf(u16); return n; }
