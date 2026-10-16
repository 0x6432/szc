fn bad() u8 { return 256; }
pub fn main() void { _=@TypeOf(bad()); }
