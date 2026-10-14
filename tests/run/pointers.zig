// exit: 79
fn read(p: *const u8) u8 { return p.*; }
fn write(p: *u8, n: u8) void { p.* = n; }
fn indirect(p: *const *const u8) u8 { return p.*.*; }
fn choose(flag: bool, a: *u8, b: *const u8) *const u8 {
    return if (flag) a else b;
}
fn cleanup(p: *u8) u8 {
    defer p.* += 3;
    p.* += 2;
    return p.*;
}
pub fn main() u8 {
    var a: u8 = 10;
    var b: u8 = 20;
    write(&a, 15);
    var p: *u8 = &a;
    p = &b;
    p.* += 5;
    const q = &p.*;
    const before = cleanup(q);
    const joined = choose(false, &a, &b);
    return read(&a) + indirect(&p) + before + joined.* - 23;
}
