// exit: 0
var g: [3:17]u16 = .{2,3,5};
const c = [_:255]u8{7,9};
const joined = c ++ [_:255]u8{11};
const mixed = c ++ [_:0]u8{13};
const b = [_:true]bool{false,true};
const known: u8 = if (c[2] != 255 or joined[3] != 255 or mixed.len != 3 or !b[2]) 256 else 0;
var effects: u8 = 0;
fn observe(p: *[2:17]u16) u16 { effects += 1; return p[2]; }
fn make(n: u16) [2:99]u16 { return .{n,n+1}; }
pub fn main() u8 {
    _ = known;
    var a: [3:17]u16 = .{7,11,13};
    if (a[3] != 17 or g[3] != 17) return 1;
    a[3] = 29;
    a = .{a[0]+1,a[1]+1,a[3]};
    if (a[0] != 8 or a[1] != 12 or a[2] != 29 or a[3] != 17) return 2;
    const p: [*:17]u16 = &a;
    if (p[3] != 17) return 3;
    const x = make(41);
    if (x[0] != 41 or x[1] != 42 or x[2] != 99) return 4;
    var flags: [1:true]bool = .{false};
    if (flags[0] or !flags[1]) return 5;
    flags = .{true};
    if (!flags[0] or !flags[1]) return 6;
    var z: [2:0]u0 = .{0,0};
    z = .{0,0};
    if (z[2] != 0) return 7;
    var e: [0:255]u8 = .{};
    if (e[0] != 255) return 8;
    e[0] = 17;
    e = .{};
    if (e[0] != 255) return 9;
    var order: [2:17]u16 = .{1,2};
    order[2] = 41;
    order = .{observe(&order),observe(&order)};
    if (effects != 2 or order[0] != 41 or order[1] != 41 or order[2] != 17) return 10;
    const plain: *[2]u16 = &order;
    plain[1] = 43;
    if (order[1] != 43 or order[2] != 17) return 11;
    const inferred: [*:17]const u8 = &.{2,3};
    const inferred_empty: [*:true]const bool = &.{};
    if (inferred[2] != 17 or !inferred_empty[0]) return 12;
    const nested: [1][0:17]u8 = .{.{}};
    const view: []const [0:17]u8 = &nested;
    const loaded = view.*;
    if (loaded[0][0] != 17) return 13;
    return 0;
}
