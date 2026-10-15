// exit: 0
var globals = [2][3]u16{.{1,2,3},.{4,5,6}};
const rows: [*][3]u16 = &globals;
const chosen = &rows[1][1];
const byte_pointer: [*]const u8 = "ab";
const outside = &byte_pointer[7];
const known = [2][2]u8{.{7,9},.{11,13}};
const known_rows: [*]const [2]u8 = &known;
const selected: u8 = known_rows[1][0];
const table = [2]*u16{&rows[0][1],chosen};
fn read(p: [*]const u16, i: usize) u16 { return p[i]; }
fn write(p: [*]u16, i: usize, n: u16) void { p[i] = n; }
fn nested(p: [*][3]u16, i: usize) void { p[i][2] = 31; }
fn choose(p: [*]u16, q: *[3]u16, c: bool) [*]u16 { return if (c) p else q; }
fn callback(n: u8) u8 { return n + 1; }
fn indirect(p: [*]const *const fn (u8) u8, i: usize) u8 { return p[i](7); }
pub fn main() u8 {
    _ = outside;
    const inferred: [*]const [2]u8 = &.{.{1,2},.{3,4}};
    if (inferred[1][0] != 3) return 9;
    if (selected != 11 or byte_pointer[2] != 0) return 1;
    if (&known_rows[1][1] != &known[1][1] or chosen != &globals[1][1]) return 2;
    var local = [3]u16{7,9,11};
    const p: [*]u16 = &local;
    write(p,1,17);
    if (read(p,1) != 17 or local[1] != 17) return 3;
    var slot: [*]u16 = p;
    slot = &globals[0];
    slot[2] = 19;
    if (globals[0][2] != 19) return 4;
    const snapshot = rows[1];
    nested(rows,1);
    if (rows[1][2] != 31 or snapshot[2] != 6) return 5;
    table[1].* = 23;
    if (chosen.* != 23 or globals[1][1] != 23) return 6;
    const callbacks = [2]*const fn (u8) u8{&callback,&callback};
    if (indirect(&callbacks,1) != 8) return 7;
    choose(p,&globals[0],false)[0] = 29;
    if (globals[0][0] != 29) return 8;
    return 0;
}
