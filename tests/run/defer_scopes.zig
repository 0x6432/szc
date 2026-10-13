// exit: 0
const constant: u8 = result: { defer {} break :result 42; };
fn check_value(x: usize) void { if (x != 8) unreachable; }
fn deferred_call() void { var value: usize = 3; defer check_value(value); value = 8; }
fn stopped(c: bool) u8 { if (c) { defer unreachable; return 1; } return 2; }
fn scopes(c: bool) usize {
    var history: usize = 0;
    {
        defer history = history * 10 + 1;
        defer history = history * 10 + 2;
        if (c) { defer history = history * 10 + 3; history = history * 10 + 4; }
        history = history * 10 + 5;
    }
    return history;
}
fn nested() usize {
    var total: usize = 0;
    { defer { defer total += 2; total += 3; } total += 1; }
    return total;
}
fn late() usize {
    var value: usize = 1;
    const saved: usize = 7;
    { defer value += saved; value = 3; }
    return value;
}
fn snapshot(x: u8) u8 { var value = x; defer value += 1; return value; }
fn local_control(n: usize) usize {
    var total: usize = 0;
    defer {}
    { defer { for (0..n) |i| { if (i == 1) continue; if (i == 3) break; total += i; } } }
    return total;
}
pub fn main() u8 {
    if (scopes(true) != 43521 or scopes(false) != 521) return 1;
    if (nested() != 6 or late() != 10 or snapshot(4) != 4) return 2;
    if (local_control(5) != 2) return 3;
    if (constant != 42 or stopped(false) != 2) return 4;
    deferred_call();
    return 0;
}
