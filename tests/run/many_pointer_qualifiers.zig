// exit: 0
var a: [4]u16 align(32) = .{7,9,11,13};
fn operation(p: [*]align(1) volatile u16, i: usize) u16 { p[i] += 2; return p[i]; }
fn weakened(p: [*]align(32) u16, i: usize) *align(2) u16 { return &p[i]; }
fn first(p: [*]align(32) u16) *align(32) u16 { return &p[0]; }
fn zero(p: [*]u0, i: usize) u0 { p[i] = 0; return p[i]; }
fn empty(p: [*]void, i: usize) void { p[i] = {}; }
pub fn main() u8 {
    const p: [*]align(32) u16 = &a;
    if (operation(p,1) != 11 or a[1] != 11) return 1;
    if (weakened(p,2) != &a[2] or first(p) != &a[0]) return 2;
    var zeros = [2]u0{0,0};
    var voids = [2]void{{},{}};
    if (zero(&zeros,99) != 0) return 3;
    empty(&voids,99);
    return 0;
}
