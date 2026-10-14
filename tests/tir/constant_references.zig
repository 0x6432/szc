const global: u8 = 23;
fn select(n: u8, c: bool) u8 {
    const x = n + 1;
    const ptr = if (c) &x else &x;
    return ptr.*;
}
fn load(p: *const *const u8) u8 { return p.*.*; }
pub fn main() u8 {
    const local: u8 = 19;
    const ptr = &local;
    return select(load(&ptr), false) + (&global).*;
}
