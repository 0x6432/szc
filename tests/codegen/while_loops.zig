fn choose(n: u8, a: u8, b: u16) u16 {
    var i: u8 = 0;
    const result = exit: while (i < n) : (i += 1) {
        if (i == 0) continue;
        if (i == 2) break :exit a;
    } else b;
    return result;
}
pub fn main() u8 { return @truncate(choose(3, 7, 300)); }
