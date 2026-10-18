const S = struct { a: u8 };
fn field(p: *const S) *const u8 {
    return &p.a;
}
pub fn main() void {
    const p: *const S = @ptrFromInt(4096);
    _ = field(p);
}
