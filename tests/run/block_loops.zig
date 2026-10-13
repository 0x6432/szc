// exit: 0
fn control(n: u8) u8 {
    var i: u8 = 0; var total: u8 = 0;
    while (i < n) : (i += 1) {
        region: {
            if (i == 0) break :region;
            if (i == 2) continue;
            if (i == 3) break;
            total += i;
        }
    }
    return total;
}
fn scan(n: u8) u8 {
    return result: {
        var i: u8 = 0;
        while (i < n) : (i += 1) { if (i == 2) break :result i; }
        break :result 19;
    };
}
fn guard(c: bool, x: u8) u8 { return value: { if (c) break :value 71; break :value @divTrunc(100, x); }; }
fn short(c: bool, x: u8) bool { return c and (value: { if (x == 0) break :value false; break :value @divTrunc(100, x) == 50; }); }
pub fn main() u8 {
    if (control(5) != 1 or control(1) != 0) return 1;
    if (scan(4) != 2 or scan(1) != 19) return 2;
    if (guard(true, 0) != 71 or guard(false, 2) != 50) return 3;
    if (short(false, 0) or short(true, 0) or !short(true, 2)) return 4;
    return 0;
}
