pub fn main() void {const p:[*]u8=@ptrFromInt(16); const s:[]u8=p[0..3]; const q:[]u16=@alignCast(@ptrCast(s)); _=q;}
