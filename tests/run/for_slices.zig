// exit: 0
var calls: u8 = 0;
fn once(s: []u16) []u16 { calls += 1; return s; }
fn sum(s: []const u16) u16 {
    var total: u16 = 0;
    for (s, 0..) |item,index| { if (index == 1) continue; total += item; }
    return total;
}
pub fn main() u8 {
    var a: [3]u16 = .{3,5,7}; var b: [3]u16 = .{11,13,17};
    if (sum(&a) != 10) return 1;
    var view: []u16 = &a;
    var total: u16 = 0;
    for (view) |item| { view = &b; total += item; }
    if (total != 15 or view[0] != 11) return 2;
    total = 0;
    for (a) |item| { total += item; a[1] = 9; }
    if (total != 15 or a[1] != 9) return 3;
    total = 0;
    for (&a) |item| { total += item; a[1] = 11; }
    if (total != 21) return 7;
    const pair: u16 = for (view,&a,2..) |x,y,index| {
        if (index == 3) break x+y;
    } else 0;
    if (pair != 24) return 4;
    var n: u8 = 0;
    for ("hi") |byte| { n += if (byte == 104) @as(u8,1) else 2; }
    if (n != 3) return 5;
    const empty: []const u8 = &.{};
    for (empty) |_| { return 6; }
    for (view) |*item| { item.* += 1; }
    if (b[0] != 12 or b[1] != 14 or b[2] != 18) return 8;
    for (&a) |*item| { item.* = 1; }
    if (a[0] != 1 or a[1] != 1 or a[2] != 1) return 9;
    for (once(view)) |*item| { item.* += 1; }
    if (calls != 1 or b[0] != 13 or b[1] != 15 or b[2] != 19) return 13;
    const nested = [2][2]u8{.{2,3},.{5,7}};
    var nested_total: u8 = 0;
    for (nested) |row| { for (row) |item| { nested_total += item; } }
    if (nested_total != 17) return 14;
    const views = [2][]const u8{"hi","x"};
    var byte_total: u16 = 0;
    for (views) |row| { for (row) |item| { byte_total += item; } }
    if (byte_total != 329) return 15;
    var cleanup: u8 = 0;
    outer: for (view) |_| {
        defer cleanup += 1;
        for (&a) |_| { break :outer; }
    }
    if (cleanup != 1) return 10;
    for (empty) |_| { return 11; } else { cleanup += 2; }
    if (cleanup != 3) return 12;
    return 0;
}
