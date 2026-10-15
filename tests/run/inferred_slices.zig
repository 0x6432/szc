// exit: 0
fn choose(c: bool, a: *[2]u16, b: *[3]u16) []u16 {
    const s = if (c) a else b;
    if (s.len != (if (c) @as(usize,2) else 3)) return &.{};
    return s;
}
fn total(c: bool, a: *const [2]u16, b: *const [3]u16) u16 {
    const s = value: { if (c) break :value a; break :value b; };
    var n: u16 = 0;
    for (s) |item| { n += item; }
    return n;
}
fn text_length(c: bool, bytes: *[4]u8) usize {
    const s = if (c) "hi" else bytes;
    return s.len;
}
pub fn main() u8 {
    var a = [2]u16{3,5}; var b = [3]u16{7,11,13};
    if (choose(true,&a,&b).len != 2 or choose(false,&a,&b).len != 3) return 1;
    if (total(true,&a,&b) != 8 or total(false,&a,&b) != 31) return 2;
    const s = choose(false,&a,&b);
    s[1] = 17;
    if (b[1] != 17) return 3;
    const empty = [0]u16{};
    const e = if (s.len == 3) &empty else &b;
    if (e.len != 0) return 4;
    var bytes = [4]u8{65,66,67,68};
    if (text_length(true,&bytes) != 2 or text_length(false,&bytes) != 4) return 5;
    return 0;
}
