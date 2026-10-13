// exit: 0
fn choose(c: bool, a: u8, b: u16) u16 {
    const result = value: { if (c) break :value a; break :value b; };
    return result;
}
fn local(c: bool, x: u8) u16 {
    const result = value: {
        if (c) { var a: u8 = x; a += 1; break :value a; }
        var b: u16 = x; b += 300; break :value b;
    };
    return result;
}
fn nested(c: bool) u8 {
    return outer: { _ = inner: { if (c) break :outer 31; break :inner 0; }; break :outer 32; };
}
fn early(c: bool) u8 { const x = value: { if (c) break :value 41; return 42; }; return x; }
fn both(c: bool) u8 { return value: { if (false) break :value 0; if (c) return 51; return 52; }; }
fn cast(c: bool, x: u16) u8 { return value: { if (c) break :value @intCast(x); break :value @truncate(x); }; }
fn assignment(c: bool, x: u16) u8 {
    var result: u8 = 0;
    result = value: { if (c) break :value @intCast(x); break :value @truncate(x); };
    return result;
}
fn identity(x: u8) u8 { return x; }
fn argument(c: bool, x: u16) u8 { return identity(value: { if (c) break :value @intCast(x); break :value @truncate(x); }); }
fn boolean(c: bool, x: bool) bool { return value: { if (c) break :value x; break :value !x; }; }
fn zero(c: bool, x: u0) u0 { return value: { if (c) break :value x; break :value 0; }; }
fn void_result(c: bool) void { value: { if (c) break :value; } }
pub fn main() u8 {
    if (choose(true, 1, 300) != 1 or choose(false, 1, 300) != 300) return 1;
    if (local(true, 7) != 8 or local(false, 7) != 307) return 2;
    if (nested(true) != 31 or nested(false) != 32) return 3;
    if (early(true) != 41 or early(false) != 42 or both(true) != 51 or both(false) != 52) return 4;
    if (cast(true, 255) != 255 or cast(false, 256) != 0) return 5;
    if (assignment(true, 255) != 255 or assignment(false, 256) != 0) return 6;
    if (argument(true, 255) != 255 or argument(false, 256) != 0) return 7;
    if (!boolean(true, true) or boolean(false, true)) return 8;
    if (zero(true, 0) != 0 or zero(false, 0) != 0) return 9;
    void_result(true); void_result(false);
    return 0;
}
