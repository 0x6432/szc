const answer = value: { break :value 7; };
fn choose(c: bool, a: u8, b: u16) u16 {
    const result = value: { if (c) { const x = a; break :value x; } break :value b; };
    return result;
}
fn nested(c: bool) u8 {
    return outer: { _ = inner: { if (c) break :outer answer; break :inner 0; }; break :outer 8; };
}
pub fn main() u8 { return if (choose(true, 3, 300) == 3) nested(false) else 9; }
