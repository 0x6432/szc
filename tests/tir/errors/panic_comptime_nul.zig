const x = @panic("a\x00b");
pub fn main() void { _ = x; }
