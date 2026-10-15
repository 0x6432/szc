const a = [2][2]u8{.{1,2},.{3,4}};
const p = &a[1][1];
const row = &a[1];
const types = [_]type{u8,u16};
const T = (&types[1]).*;
const zeros = [_]u0{0,0};
const same_zero = &zeros[0] == &zeros[1];
const different = &a[0][0] != p;
fn read(ptr: *const u8) u8 { return ptr.*; }
pub fn main() u8 {
    if (!same_zero or !different or p != &row[1]) return 1;
    if (@as(T,7) != 7) return 2;
    return read(p);
}
