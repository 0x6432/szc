// exit: 0
fn signed5(n: u5) i5 { return @bitCast(n); }
fn unsigned5(n: i5) u5 { return @bitCast(n); }
fn signed33(n: u33) i33 { return @bitCast(n); }
fn unsigned33(n: i33) u33 { return @bitCast(n); }
fn signed64(n: u64) i64 { return @bitCast(n); }
fn unsigned64(n: i64) u64 { return @bitCast(n); }
fn signed_bool(n: bool) i1 { return @bitCast(n); }
fn bool_signed(n: i1) bool { return @bitCast(n); }
fn bool_unsigned(n: u1) bool { return @bitCast(n); }
fn unsigned_bool(n: bool) u1 { return @bitCast(n); }
fn nested(n: u5) u5 { const mid: i5 = @bitCast(n); return @bitCast(mid); }
fn extend(n: u5) i64 { const mid: i5 = @bitCast(n); return mid; }
fn empty(n: u0) void { return @bitCast(n); }
fn zero(n: void) u0 { return @bitCast(n); }
const negative128: i128 = @bitCast(@as(u128, 0xffffffffffffffffffffffffffffffff));
const positive257: u257 = @bitCast(@as(i257, -1));
pub fn main() u8 {
    if (signed5(31) != -1 or signed5(16) != -16 or signed5(15) != 15) return 1;
    if (unsigned5(-1) != 31 or unsigned5(-16) != 16) return 2;
    if (signed33(0x1ffffffff) != -1 or signed33(0x100000000) != -4294967296) return 3;
    if (unsigned33(-1) != 0x1ffffffff) return 4;
    if (signed64(0xffffffffffffffff) != -1 or unsigned64(-1) != 0xffffffffffffffff) return 5;
    if (signed_bool(true) != -1 or signed_bool(false) != 0) return 6;
    if (!bool_signed(-1) or bool_signed(0) or !bool_unsigned(1) or bool_unsigned(0)) return 7;
    if (unsigned_bool(true) != 1 or unsigned_bool(false) != 0) return 8;
    if (nested(31) != 31 or extend(31) != -1) return 9;
    if (negative128 != -1 or positive257 != (1 << 257) - 1) return 10;
    empty(zero({}));
    const undef: u8 = @bitCast(@as(i8, undefined));
    _ = undef;
    return 0;
}
