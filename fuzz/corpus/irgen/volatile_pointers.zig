// exit: 0
var value: u8 align(64) = 7;
const saved: *align(64) volatile u8 = &value;
fn qualify(p: *u8) *volatile const u8 { return p; }
fn choose(flag: bool, p: *u8, q: *volatile const u8) *volatile const u8 { return if (flag) p else q; }
fn target(p: *volatile const u8) void { _ = p; }
fn invoke(f: *const fn (*const u8) void, p: *const u8) void { f(p); }
pub fn main() u8 {
    const p: *allowzero const volatile u8 = saved;
    const q: *volatile const u8 = &value;
    const slot: *const *align(64) volatile u8 = &saved;
    if (slot.* != saved or qualify(&value) != q or choose(true, &value, q) != q) return 1;
    if (p != &value or &q.* != q) return 2;
    invoke(target, &value);
    return 0;
}
