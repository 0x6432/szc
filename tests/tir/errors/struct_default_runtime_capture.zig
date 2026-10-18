fn use(v: u8) u8 {
    const n: u8 = v;
    const S = struct { x: u8 = n };
    return (S{}).x;
}
pub fn main() u8 { return use(7); }
