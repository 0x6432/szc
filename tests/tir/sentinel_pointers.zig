// exit: 0
const text: [*:0]const u8 = "hello";
var cursor: [*:0]const u8 = "world";
const words = [2][*:0]const u8{"abc","xy"};
const shifted = text + 2;
const known: u8 = if (shifted[3] != 0 or shifted[0] != 108) 256 else 0;
fn length(p: [*:0]const u8) usize {
    var n: usize = 0;
    while (p[n] != 0) : (n += 1) {}
    return n;
}
fn advance(p: [*:0]const u8, n: usize) [*:0]const u8 { return p + n; }
fn retreat(p: [*:0]const u8, n: usize) [*:0]const u8 { return p - n; }
fn tail(p: [*:0]const u8, n: usize) [*:0]const u8 { return p[n..]; }
fn choose(c: bool, a: [*:0]const u8, b: [*]const u8) [*]const u8 { return if(c) a else b; }
fn window(p: [*:0]const u8, lo: usize, hi: usize) []const u8 { return p[lo..hi]; }
fn bool_type(p: [*:true]bool) [*:true]bool { return p; }
pub fn main() u8 {
    _ = known;
    _ = bool_type;
    if (length(text) != 5 or length("") != 0 or length("a\x00b") != 1) return 1;
    if (length(words[0]) != 3 or length(words[1]) != 2) return 2;
    cursor += 2;
    if (length(cursor) != 3 or cursor[0] != 114) return 3;
    cursor -= 1;
    if (length(cursor) != 4 or cursor[0] != 111) return 4;
    const p = advance(text,2);
    if (length(p) != 3 or retreat(p,1)[0] != 101) return 5;
    if (tail(text,3)[0] != 108 or length(tail(text,5)) != 0) return 6;
    const s = window(text,1,4);
    if (s.len != 3 or s[2] != 108) return 7;
    const a: *const u8 = &text[5];
    if (a.* != 0 or text[0..2][1] != 101) return 8;
    if (choose(false,text,"z")[0] != 122) return 9;
    return 0;
}
