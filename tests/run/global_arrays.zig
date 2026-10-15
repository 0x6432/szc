// exit: 0
var a: [2][3]u8 = .{.{1,2,3},.{4,5,6}};
var b = [_]i33{-7,8};
var empty: [0]u16 = .{};
var zeros: [3]u0 = .{0,0,0};
var units: [2]void = .{{},{}};
var uninitialized: [3]u16 = undefined;
var counter: u8 = 0;
const length = a.len;
const row_length = a[0].len;
var undefined_pointer: *[2]u8 = undefined;
const pointer_length = undefined_pointer.len;
const row_pointer = &a[1];
const saved_element = &a[1][2];
const read_only: *const u8 = &a[0][0];
fn index() usize { a[1][0] = 19; return 0; }
fn read(p: *const u8) u8 { return p.*; }
fn generated() [2]u8 { counter += 1; return .{7,9}; }
fn snapshot() [2][3]u8 { return a; }
fn make(n: u8) [2][3]u8 { return .{.{n,2,3},.{4,5,6}}; }
fn cleanup() [2][3]u8 { defer a[0][0] = 31; return a; }
pub fn main() u8 {
    if (length != 2 or row_length != 3 or pointer_length != 2) return 1;
    const before = snapshot();
    a[0][0] = 7;
    if (before[0][0] != 1 or read(read_only) != 7) return 2;
    saved_element.* = 23;
    if (a[1][2] != 23 or row_pointer[2] != 23 or saved_element != &a[1][2]) return 3;
    if (a[1][index()] != 19) return 4;
    a = make(11);
    const saved = cleanup();
    if (saved[0][0] != 11 or a[0][0] != 31) return 5;
    a[0] = .{9,a[0][0],7};
    if (a[0][0] != 9 or a[0][1] != 9) return 6;
    a[0] = [_]u8{10,a[0][0],8};
    if (a[0][0] != 10 or a[0][1] != 9) return 7;
    a = a;
    b[0] -= 1;
    if (b[0] != -8 or b[1] != 8) return 8;
    uninitialized = .{258,259,260};
    if (uninitialized[1] != 259) return 9;
    empty = .{};
    zeros[2] = 0;
    units[1] = {};
    if (empty.len != 0 or zeros[2] != 0) return 10;
    _ = units[0];
    if (generated().len != 2 or counter != 1) return 11;
    return 0;
}
