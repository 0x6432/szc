// exit: 0
fn counted(n: usize) usize {
    var history: usize = 0;
    for (0..n) |i| {
        defer history = history * 10 + i + 1;
        if (i == 0) continue;
        if (i == 2) break;
    } else history = history * 10 + 9;
    return history;
}
fn while_continue(n: usize) usize {
    var total: usize = 0; var i: usize = 0;
    while (i < n) : (i += 1) { defer total += i + 1; if (i == 0) continue; if (i == 2) break; }
    return total * 10 + i;
}
fn labeled() usize {
    var total: usize = 0;
    outer: for (0..3) |i| {
        defer total += 10;
        for (0..3) |j| {
            defer total += 1;
            if (i == 0 and j == 0) continue :outer;
            if (i == 1 and j == 1) break :outer;
        }
    }
    return total;
}
fn loop_value(n: usize) usize {
    var total: usize = 0;
    const result: usize = for (0..n) |i| { defer total += 10; if (i == 1) break total; } else total;
    return total + result;
}
pub fn main() u8 {
    if (counted(0) != 9 or counted(2) != 129 or counted(4) != 123) return 1;
    if (while_continue(4) != 62 or while_continue(2) != 32) return 2;
    if (labeled() != 23 or loop_value(4) != 30 or loop_value(1) != 20) return 3;
    return 0;
}
