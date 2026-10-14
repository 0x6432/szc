pub fn main() void {
    const p: *allowzero u8 = undefined;
    const q: *u8 = p;
    _ = q;
}
