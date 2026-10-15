const a: [8388608]u8 = undefined;
const b: [8388608]u8 = undefined;
const c = [_]u8{7};
fn length(p: *const [8388608]u8) usize { return p.len; }
fn tail(p: *const [1]u8) usize { return p.len; }
pub fn main() void { _ = length(&a); _ = length(&b); _ = tail(&c); }
