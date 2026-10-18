const S = struct { a: u8 };
const obj = S{ .a = 7 };
pub fn main() u8 { const p: *const volatile S = &obj; return @field(p, "a"); }
