const S = struct { x: u16 };
var g: [18446744073709551615]S = undefined;
pub fn main() void { _ = &g; }
