pub fn main() u8 {
    var a: [2]u8 = .{1,2};
    const s: []u8 = &a;
    for (s,0..) |*item,i| { item.* += @intCast(i); }
    var total: u8 = 0;
    for (a) |item| { total += item; }
    return total;
}
