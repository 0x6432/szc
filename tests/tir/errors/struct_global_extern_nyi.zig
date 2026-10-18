const S = struct { x: u8 };
extern var g: S;
pub fn main() void { _ = &g; }
