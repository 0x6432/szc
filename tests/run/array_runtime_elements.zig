// exit: 0
fn add(n: u8) u8 { return n + 1; }
fn test_elements(n: u8) u8 {
    var x: u8 = n;
    var y: u8 = n + 1;
    var pointers = [_]*u8{&x, &y};
    const before = pointers;
    pointers[0] = &y;
    pointers[0].* = 17;
    if (before[0].* != n or before[1].* != 17) return 1;
    const callbacks = [_]*const fn (u8) u8{&add, &add};
    var funcs = callbacks;
    funcs[0] = callbacks[1];
    if (funcs[0](n) != n + 1) return 2;
    var wide = [_]i33{-17, 31};
    const wide_old = wide;
    wide[1] = -9;
    if (wide_old[1] != 31 or wide[1] != -9) return 3;
    var values = [_]u8{n, 9};
    const volatile_values: *volatile [2]u8 = &values;
    volatile_values[1] = 21;
    if (volatile_values[1] != 21) return 4;
    return 0;
}
pub fn main() u8 { return test_elements(7); }
