// exit: 0
fn flip(p: *bool) void { p.* = !p.*; }
fn read(p: *const bool) bool { return p.*; }
fn pick(c: bool, a: *u8, b: *const u8) *const u8 {
    return selected: {
        if (c) break :selected a;
        break :selected b;
    };
}
fn mutate(p: *u8) void {
    for (0..4) |i| {
        defer p.* += 1;
        if (i == 0) continue;
        if (i == 2) break;
        p.* += 2;
    }
}
pub fn main() u8 {
    var flag = false;
    flip(&flag);
    if (!read(&flag)) return 1;
    var a: u8 = 10;
    var b: u8 = 20;
    mutate(&a);
    b += 1;
    if (pick(true, &a, &b).* != 15) return 2;
    if (pick(false, &a, &b).* != 21) return 3;
    return 0;
}
