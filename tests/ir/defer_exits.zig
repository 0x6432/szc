fn choose(c: bool, x: u8) u8 {
    var value = x;
    const saved: u8 = done: {
        defer { const next = value +% 1; value = next; }
        if (c) break :done value;
        value +%= 2;
        break :done value;
    };
    return saved +% value;
}
pub fn main() u8 { return choose(true, 3); }
