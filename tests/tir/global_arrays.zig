var a: [2][2]u8 = .{.{1,2},.{3,4}};
const pointer = &a[1][0];
const length = a.len;
fn index() usize { a[1][0] = 9; return 0; }
pub fn main() u8 {
    const saved = a;
    a[0][0] = 7;
    pointer.* = 5;
    if (length != 2) return 1;
    return saved[0][0] + a[1][index()] + pointer.*;
}
