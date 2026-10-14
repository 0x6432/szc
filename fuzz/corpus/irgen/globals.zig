// exit: 0
const Number = u16;
var counter: Number = 13;
var derived: u16 = blk: { break :blk 2 + 3; };
var flag = false;
var selected = &counter;
var other: u16 = 29;
const live = &counter;
const nested = &live;
var forward: *u8 = &later;
var later: u8 = if (&later == &later) 7 else 0;
var cycle: *u8 = &cycle_value;
var cycle_value: u8 = if (&cycle == &cycle) 11 else 0;
var backed: *const u16 = &@as(u16, 41);
var zero: u0 = 0;
var empty: void = {};
const zero_read = zero;
const zero_written = blk: { zero = 0; break :blk @as(u8, 0); };
// Unreferenced globals remain lazy, including an invalid inferred integer type.
var unused_invalid = 1;
fn read_nested(p: *const *const u16) u16 { return p.*.*; }
fn read(p: *const u16) u16 { return p.*; }
fn advance(n: u8) void {
    if (n == 0) return;
    counter += 1;
    advance(n - 1);
}
fn snapshot() u16 {
    const saved = counter;
    counter += 1;
    return read(&saved);
}
fn cleanup() u16 {
    defer counter += 3;
    return counter;
}
pub fn main() u8 {
    if (counter != 13 or flag) return 1;
    selected.* += 5;
    if (live.* != 18 or read_nested(nested) != 18 or read(&counter) != 18) return 2;
    selected = &other;
    selected.* = 31;
    if (other != 31 or counter != 18 or selected == live) return 3;
    advance(4);
    if (cleanup() != 22 or counter != 25) return 4;
    flag = true;
    if (!flag) return 5;
    if (forward != &later or forward.* != 7 or cycle.* != 11) return 6;
    forward.* = 9;
    if (later != 9 or backed.* != 41) return 7;
    backed = &@as(u16, 43);
    if (backed.* != 43) return 8;
    zero = zero_read;
    empty = {};
    if (snapshot() != 25 or counter != 26 or derived != 5) return 9;
    return zero_written;
}
