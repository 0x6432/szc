// exit: 6
fn identity(a: bool) bool {
    return a;
}
fn fail() bool {
    unreachable;
}
fn combine(a: bool, b: bool) bool {
    return a and b;
}
pub fn main() u8 {
    const a = identity(false) and fail();
    const b = identity(true) or fail();
    const c = identity(true) and identity(true);
    const d = identity(false) or identity(true);
    const e = (identity(true) and identity(false)) or identity(true);
    const f = combine(true, true);
    const g = !identity(false);
    return @as(u8, @intFromBool(a)) + @intFromBool(b) + @intFromBool(c) + @intFromBool(d) + @intFromBool(e) + @intFromBool(f) + @intFromBool(g);
}
