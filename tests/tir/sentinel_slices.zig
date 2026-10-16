// exit: 0
const bytes: [:0]const u8 = "hello";
const values: [:17]const u16 = &.{2,3,5};
var backing: [3:17]u16 = .{7,11,13};
var global: [:17]u16 = &backing;
fn at(s: [:17]const u16, i: usize) u16 { return s[i]; }
fn tail(s: [:17]u16, i: usize) [:17]u16 { return s[i..]; }
fn pick(c: bool) [:0]const u8 { const s = if(c) "a" else "long"; return s; }
fn window(s: [:17]u16, i: usize, j: usize) []u16 { return s[i..j]; }
fn zero(s: [:0]const u0, n: usize) u0 { return s[n]; }
fn run(i: usize) u8 {
    if (bytes.len != 5 or bytes[5] != 0 or values[3] != 17) return 1;
    if (at(values,values.len) != 17 or at(global,3) != 17) return 2;
    var s = tail(global,i);
    if (s.len != 2 or s[2] != 17) return 3;
    s[0] = 23;
    if (backing[1] != 23) return 4;
    const p: [*:17]u16 = s.ptr;
    if (p[2] != 17) return 5;
    const ptr_field: *[*:17]u16 = &s.ptr;
    ptr_field.* = &backing;
    s.len = 3;
    if (s[0] != 7 or s[3] != 17) return 6;
    const end = window(s,i,4);
    if (end.len != 3 or end[2] != 17) return 7;
    const empty = tail(s,3);
    if (empty.len != 0 or empty[0] != 17) return 8;
    const known = values[1..];
    const a = known.*;
    if (a.len != 2 or a[2] != 17) return 9;
    const long = pick(false);
    if (long.len != 4 or long[4] != 0) return 10;
    const plain: []const u16 = values;
    if (plain.len != 3 or plain[2] != 5) return 11;
    const flags: [:true]const bool = &.{false};
    if (flags[0] or !flags[1]) return 12;
    global = &backing;
    if (global.ptr != &backing or global.len != 3) return 13;
    const zero_values = [2:0]u0{0,0};
    if (zero(&zero_values,2) != 0) return 14;
    const zero_slice: [:0]const u0 = &zero_values;
    const zero_tail = zero_slice[1..];
    if (zero_tail[zero_tail.len] != 0) return 15;
    return 0;
}
pub fn main() u8 { return run(1); }
