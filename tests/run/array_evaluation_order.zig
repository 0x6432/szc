// exit: 0
fn index(p: *[2]u8) usize { p[0] = 9; return 0; }
fn read(p: *[2]u8) u8 { return p[0]; }
fn next(p: *u8) u8 { p.* += 1; return p.*; }
fn nested_index(p: *[2][2]u8) usize { p[0][0] = 23; return 0; }
fn test_order(n: u8) u8 {
    var a = [_]u8{n, 2};
    if (a[index(&a)] != 9) return 1;
    a = [_]u8{n, 2};
    const p = &a;
    if (p.*[index(p)] != 9) return 2;
    a = [_]u8{n, 2};
    a = .{9, read(&a)};
    if (a[1] != 9) return 3;
    a = [_]u8{n, 2};
    a = [_]u8{9, read(&a)};
    if (a[1] != n) return 4;
    a = [_]u8{n, 2};
    a = @as([2]u8, .{9, read(&a)});
    if (a[1] != n) return 5;
    var x: u8 = n;
    const b = [_]u8{next(&x), next(&x)};
    if (b[0] != n + 1 or b[1] != n + 2) return 6;
    var c: [2][2]u8 = .{.{n, 2}, .{3, 4}};
    if (c[0][nested_index(&c)] != 23) return 7;
    c = .{.{9, c[0][0]}, .{read(&a), 4}};
    if (c[0][1] != 9) return 8;
    return 0;
}
pub fn main() u8 { return test_order(1); }
