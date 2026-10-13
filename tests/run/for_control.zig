// exit: 0
fn labeled() usize {
    var total: usize = 0;
    outer: for (0..4) |i| {
        for (0..3) |j| { if (i == 0 and j == 1) continue :outer; if (i == 2) break :outer; total += 1; }
    }
    return total;
}
fn value(n: usize) u8 { return for (0..n) |i| { if (i == 0) break 7; } else 8; }
fn early(n: usize) u8 { const x = for (0..n) |_| { break 9; } else return 10; return x; }
fn peer(n: usize, a: u8, b: u16) u16 { const x = for (0..n) |_| { break a; } else b; return x; }
fn typed(n: usize, x: u16) u8 { return for (0..n) |_| { break @intCast(x); } else @truncate(x); }
fn exhausted(n: usize) usize { var total: usize = 0; for (0..n) |i| total += i else total += 10; return total; }
fn outer_else(c: bool) usize {
    var total: usize = 0;
    for (0..3) |_| { for (0..0) |_| {} else { if (c) continue; break; } total += 100; }
    return total;
}
fn block_exit(n: usize) u8 { return done: { for (0..n) |i| { if (i == 2) break :done @intCast(i); } break :done 19; }; }
fn named_else() u8 { return target: for (0..0) |_| {} else { break :target 17; }; }
pub fn main() u8 {
    if (labeled() != 4) return 1;
    if (value(1) != 7 or value(0) != 8 or early(1) != 9 or early(0) != 10) return 2;
    if (peer(1, 3, 300) != 3 or peer(0, 3, 300) != 300) return 3;
    if (typed(1, 255) != 255 or typed(0, 256) != 0) return 4;
    if (exhausted(3) != 13 or exhausted(0) != 10 or outer_else(true) != 0 or outer_else(false) != 0) return 5;
    if (block_exit(4) != 2 or block_exit(1) != 19 or named_else() != 17) return 6;
    return 0;
}
