// exit: 0
fn sum(start: usize, end: usize) usize {
    var total: usize = 0;
    for (start..end) |i| { if (i == 2) continue; if (i == 5) break; total += i; }
    return total;
}
fn counted(n: usize) usize { var total: usize = 0; for (0..n) |_| total += 1; return total; }
fn nested(n: usize) usize { var total: usize = 0; for (0..n) |i| { for (0..i + 1) |j| total += j; } return total; }
fn snapshot() usize {
    var end: usize = 4; var origin: usize = 5; var total: usize = 0;
    for (1..end, origin..) |i, j| { end = 1; origin += 1; total += i + j; }
    return total;
}
fn order() bool {
    var history: usize = 0; var total: usize = 0;
    for (a: { history = history * 10 + 1; break :a 0; }..b: { history = history * 10 + 2; break :b 2; },
         c: { history = history * 10 + 3; break :c 3; }..d: { history = history * 10 + 4; break :d 5; }) |i, j| { total += j - i; }
    return history == 1234 and total == 6;
}
fn top(start: usize, end: usize) usize { var last: usize = 0; for (start..end) |i| last = i; return last; }
pub fn main() u8 {
    if (sum(0, 8) != 8 or sum(0, 3) != 1 or sum(4, 4) != 0) return 1;
    if (counted(0) != 0 or counted(4) != 4 or nested(3) != 4) return 2;
    if (snapshot() != 24 or !order()) return 3;
    if (top(0xfffffffffffffffd, 0xffffffffffffffff) != 0xfffffffffffffffe) return 4;
    return 0;
}
