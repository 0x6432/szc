const S = struct { a:u8, b:u64, c:u16, };
fn advance(p:[*]S, n:usize) usize { return @intFromPtr(p+n); }
pub fn main() u8 { return if (advance(@ptrFromInt(4096),3)==4144) 0 else 1; }
