// exit: 0
// Zig 0.17.0 sema infers u16 here, but its native backend reports
// "genSetReg called with a value larger than dst_reg". Keep this native
// regression separate; tools/diff_slice_deref.py checks the inferred type.
var a: [3]u16 = .{2,3,5};
const s: []u16 = &a;
pub fn main() u8 {
    const value: u16 = s.*;
    if (value != 2) return 1;
    a[0] = 7;
    const changed: u16 = s.*;
    if (changed != 7) return 2;
    return 0;
}
