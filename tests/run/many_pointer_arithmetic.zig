// exit: 0
var backing: [2][3]u16 align(64) = .{.{1,2,3},.{4,5,6}};
const start: [*]align(1) u16 = &backing[0];
const tail = start + 4;
const previous = tail - 1;
const targets: [2][*]align(1) u16 = .{tail,previous};
const known = [3]u16{7,9,11};
const known_start: [*]const u16 = &known;
const known_tail = known_start + 2;
const known_back = known_tail - 1;
const known_delta = known_tail - known_start;
const bytes: [*]const u8 = "abc";
const end = bytes + 3;
const types = [2]type{u8,u16};
const type_pointer: [*]const type = &types;
const second_type = type_pointer + 1;
const number: second_type[0] = 65535;
fn next(p: [*]align(1) u16, n: usize) [*]align(1) u16 { return p + n; }
fn back(p: [*]align(1) u16, n: usize) [*]align(1) u16 { return p - n; }
fn difference(p: [*]align(1) const u16, q: [*]align(1) const u16) usize { return p - q; }
fn element_difference(p: *align(1) const u16, q: *align(1) const u16) usize { return p - q; }
fn row_difference(p: [*][3]u16, q: [*][3]u16) usize { return p - q; }
fn roundtrip(p: [*]u0, n: usize) [*]u0 { return (p + n) - n; }
pub fn main() u8 {
    var cursor: [*]align(1) u16 = start;
    cursor += @intCast(@as(u8,4));
    cursor -= 1;
    if (cursor != previous) return 8;
    if (known_back[0] != 9 or known_delta != 2 or end[0] != 0 or number != 65535) return 1;
    if (&known_back[0] != &known[1] or (known_tail - &known) != 2) return 2;
    if (next(start,4) != tail or back(tail,1) != previous) return 3;
    if (difference(tail,start) != 4 or element_difference(&backing[1][2],&backing[0][0]) != 5) return 4;
    targets[0][0] = 31;
    if (backing[1][1] != 31 or targets[1][0] != 4) return 5;
    const rows: [*][3]u16 = &backing;
    if (row_difference(rows+1,rows) != 1 or (rows+1)[0][1] != 31) return 6;
    var zeros = [2]u0{0,0};
    const zp: [*]u0 = &zeros;
    if (roundtrip(zp,99) != zp) return 7;
    return 0;
}
