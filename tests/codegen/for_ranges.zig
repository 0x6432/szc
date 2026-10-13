fn choose(n: usize, a: u8, b: u16) u16 {
    const result = target: for (0..n, 5..8) |i, j| {
        if (i == 0) continue;
        if (j == 7) break :target a;
    } else b;
    return result;
}
pub fn main() u8 { return @truncate(choose(3, 7, 300)); }
