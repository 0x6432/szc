// exit: 0
const a = [5]u16{2,3,5,7,11};
const view = a[1..4];
const copy = view.*;
const text = "hello"[1..4];
const word = text.*;
fn window(s: []u16, lo: usize, hi: usize) []u16 { return s[lo..hi]; }
fn fixed(s: []u16) *[2]u16 { return s[1..3]; }
fn tail(p: [*]u16, i: usize) [*]u16 { return p[i..]; }
var original = [3]u16{31,37,41};
var replacement = [3]u16{43,47,53};
var current: []u16 = &original;
var evaluations: u8 = 0;
fn operand() *[]u16 { evaluations += 1; return &current; }
fn lower() usize { evaluations += 1; current = &replacement; return 1; }
fn upper() usize { evaluations += 1; return 3; }
fn empty(s: []u16, n: usize) []u16 { return s[n..n]; }
pub fn main() u8 {
    if (copy[0] != 3 or copy[2] != 7 or word[0] != 101 or word[2] != 108) return 1;
    if (&view[0] != &a[1] or &text[1] != &"hello"[2]) return 4;
    const full = "hello"[1..];
    if (full[4] != 0) return 5;
    const nested = view[1..2];
    if (nested.*[0] != 5 or &nested[0] != &a[2]) return 6;
    const types = [3]type{u8,u16,u32};
    const T = types[1..3][0];
    if (@as(T,257) != 257) return 7;
    var b = [4]u16{13,17,19,23};
    const s = window(&b,1,3);
    s[0] = 29;
    if (b[1] != 29 or fixed(&b)[1] != 19) return 2;
    const many: [*]u16 = &b;
    if (tail(many,2)[1] != 23) return 3;
    const later = operand().*[lower()..upper()];
    if (evaluations != 3 or later[0] != 47 or later.len != 2) return 8;
    later[1] = 59;
    if (replacement[2] != 59 or original[2] != 41) return 9;
    const zero = empty(&b,4);
    if (zero.len != 0 or @intFromPtr(zero.ptr) != @intFromPtr(many + 4)) return 10;
    const nul = "hello"[5..6];
    if (nul[0] != 0) return 11;
    return 0;
}
