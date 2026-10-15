pub fn main() void { var n: u8 = 0; _ = &n; const P = [*:n]u8; _ = P; }
