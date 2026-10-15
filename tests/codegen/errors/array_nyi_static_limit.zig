const a: [16777217]u8 = undefined;
fn length(p: *const [16777217]u8) usize { return p.len; }
pub fn main() void { _ = length(&a); }
