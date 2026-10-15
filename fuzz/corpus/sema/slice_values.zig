// exit: 0
var data: [4]u16 = .{3,5,7,9};
const greeting: []const u8 = "hello";
var global_view: []u16 = &data;
fn borrow(p: *[4]u16) []u16 { return p; }
fn choose(a: []u16, b: []u16, flag: bool) []u16 { return if (flag) a else b; }
fn update(s: []u16, index: usize) u16 { s[index] += 10; return s[index]; }
fn inspect(s: []const u16) u16 { return s[0] + @as(u16,@intCast(s.len)); }
pub fn main() u8 {
    const s = borrow(&data);
    if (s.len != 4 or @intFromPtr(s.ptr) != @intFromPtr(&data[0])) return 1;
    if (update(s,2) != 17 or data[2] != 17) return 2;
    var t: []u16 = s;
    const copy = t;
    var other: [4]u16 = .{11,13,15,17};
    t = &other;
    if (copy.ptr != s.ptr or t.ptr == copy.ptr) return 3;
    const selected = choose(t,copy,false);
    if (selected.ptr != s.ptr or inspect(t) != 15) return 4;
    if (greeting.len != 5 or greeting[1] != 101) return 5;
    if (global_view.len != 4 or global_view[2] != 17) return 6;
    global_view = &other;
    if (global_view.ptr != t.ptr or global_view[0] != 11) return 7;
    if (@sizeOf([]u16) != 16 or @alignOf([]u16) != 8) return 8;
    var views: [2][]u16 = .{s,t};
    views[0][1] = 21;
    if (data[1] != 21 or views[1].len != 4) return 9;
    const empty: []align(64) u8 = &.{};
    if (empty.len != 0) return 10;
    var descriptor = &t;
    _ = &descriptor;
    if (descriptor.len != 4 or descriptor.*.ptr != t.ptr) return 11;
    return 0;
}
