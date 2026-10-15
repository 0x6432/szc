// exit: 0
var a: [4]u16 = .{2,3,5,7};
var s: []u16 = &a;
const length_address = &s.len;
const data_address = &s.ptr;
const texts = [2][]const u8{"hello","xy"};
const text_len = &texts[1].len;
const text_ptr = &texts[1].ptr;
const known: u8 = if (text_len.* != 2 or text_ptr.*[1] != 121) 256 else 0;
var headers: [2][]u16 = .{&a,&a};
const nested_len = &headers[1].len;
var calls: u8 = 0;
fn get() *[]u16 { calls += 1; return &s; }
fn borrow(p: *[]u16) *usize { return &p.len; }
fn snapshot(value: []u16, original: *[]u16) usize { original.len = 1; return value.len; }
pub fn main() u8 {
    _ = known;
    if (length_address.* != 4 or data_address.*[1] != 3) return 1;
    length_address.* = 3;
    data_address.* += 1;
    if (s.len != 3 or s[0] != 3) return 2;
    get().len -= 1;
    if (calls != 1 or s.len != 2) return 3;
    if (snapshot(s,&s) != 2 or s.len != 1) return 4;
    borrow(&s).* = 3;
    s[1] = 11;
    if (a[2] != 11 or s.len != 3) return 5;
    nested_len.* = 2;
    headers[0].ptr += 2;
    if (headers[1].len != 2 or headers[0].ptr[0] != 11) return 6;
    var unaligned: []u16 align(1) = &a;
    const p: *align(1) usize = &unaligned.len;
    p.* = 2;
    unaligned.ptr += 1;
    if (unaligned.len != 2 or unaligned[0] != 3) return 7;
    var initialized: []u16 = undefined;
    initialized.ptr = &a;
    initialized.len = 4;
    if (initialized[2] != 11) return 8;
    return 0;
}
