const S = struct { a: u128 };
const obj = S{ .a = 7 };
fn use(p: *const anyopaque) void {
    _ = p;
}
pub fn main() void {
    use(@ptrCast(&obj));
}
