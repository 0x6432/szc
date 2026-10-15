// exit: 0
const a: [2][3]u16 = .{.{1,2,3},.{4,5,6}};
const p = &a[1][2];
const row = &a[1];
const row_ptr = &row;
const same = p == &row_ptr.*[2];
const distinct = &a[0][2] != p;
const copy = a[1];
const copy_distinct = &copy[2] != p;
const types = [_]type{u8,u16};
const type_pointer = &types[1];
const T = type_pointer.*;
const type_distinct = &types[0] != &types[1];
const zeros = [_]u0{0,0};
const zero_equal = &zeros[0] == &zeros[1];
const text = "ab";
const plain: *const [2]u8 = text;
const text_pointer = &text[1];
const plain_pointer = &plain[1];
const sentinel = &text[2];
const table = [_]*const u16{&a[0][2],p};
var saved: *const u16 = p;
fn read(ptr: *const u16) u16 { return ptr.*; }
fn equal(x: *const u16, y: *const u16) bool { return x == y; }
fn text_equal(x: *const u8, y: *const u8) bool { return x == y; }
fn from_table(index: usize) u16 { return read(table[index]); }
fn row_value(ptr: *const [3]u16) [3]u16 { return ptr.*; }
fn callbacks(ptr: *const u16) u8 { return @intCast(ptr.*); }
const functions = [_]*const fn (*const u16) u8{&callbacks};
const callback_pointer = &functions[0];
const bodies = [_]fn (*const u16) u8{callbacks,callbacks};
const body_pointer = &bodies[1];
const type_rows = [2][2]type{.{u8,u8},.{u8,u8}};
const type_row = &type_rows[1];
const RowT = (&type_row[1]).*;
fn invoke(ptr: *const *const fn (*const u16) u8) u8 { return ptr.*(p); }
pub fn main() u8 {
    if (!same or !distinct or !copy_distinct or !type_distinct or !zero_equal) return 1;
    if (read(p) != 6 or from_table(0) != 3 or from_table(1) != 6) return 2;
    if (!equal(p,&row[2]) or equal(p,&a[0][2])) return 3;
    if (!text_equal(text_pointer,plain_pointer) or sentinel.* != 0) return 4;
    if (@as(T,7) != 7 or p.* != 6 or row_value(row)[1] != 5) return 5;
    if (read(saved) != 6 or invoke(callback_pointer) != 6) return 6;
    saved = &a[0][0];
    if (read(saved) != 1) return 7;
    if (body_pointer == &bodies[0] or body_pointer.*(p) != 6) return 8;
    if (&type_rows[0][0] == &type_rows[1][0] or @as(RowT,7) != 7) return 9;
    return 0;
}
