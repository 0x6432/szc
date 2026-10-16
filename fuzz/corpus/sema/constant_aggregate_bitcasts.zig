// exit: 0
const bytes: [2]u8 = @bitCast(@as(u16,0x1234));
const narrow: [2]u5 = @bitCast(@as(u10,65));
const signs: [2]i5 = @bitCast(@as(u10,63));
const nested: [2][2]u5 = @bitCast(@as(u20,1+(2<<5)+(3<<10)+(4<<15)));
const booleans: [3]bool = @bitCast(@as(u3,5));
const with_sentinel: [2:17]u8 = @bitCast(@as(u24,0xff0201));
const from_sentinel: u24 = @bitCast(@as([2:17]u8,.{1,2}));
const partial: u16 = @bitCast(@as([2]u8,.{1,undefined}));
const zero: [2]u0 = @bitCast(@as(void,{}));
const wide: [2]u128 = @bitCast(@as(u256,(1<<255)+7));
const wide_back: u256 = @bitCast(wide);
const nested_back: u20 = @bitCast(nested);
const signed_back: i10 = @bitCast(@as([2]i5,.{-1,-1}));
pub fn main() u8 {
    if (bytes[0]!=0x34 or bytes[1]!=0x12) return 1;
    if (narrow[0]!=1 or narrow[1]!=2 or signs[0]!=-1 or signs[1]!=1) return 2;
    if (nested[0][0]!=1 or nested[0][1]!=2 or nested[1][0]!=3 or nested[1][1]!=4) return 3;
    if (!booleans[0] or booleans[1] or !booleans[2]) return 4;
    if (with_sentinel[0]!=1 or with_sentinel[1]!=2 or with_sentinel[2]!=17 or from_sentinel!=0x110201) return 5;
    if (partial!=1 or zero[0]!=0 or zero[1]!=0) return 6;
    if (wide[0]!=7 or wide[1]!=(1<<127) or wide_back!=(1<<255)+7) return 7;
    if (nested_back!=1+(2<<5)+(3<<10)+(4<<15) or signed_back!=-1) return 8;
    const undefined_with_sentinel:u24=@bitCast(@as([2:17]u8,.{undefined,undefined}));
    const nested_undefined:u48=@bitCast(@as([2][2:17]u8,.{.{undefined,undefined},.{1,2}}));
    if (undefined_with_sentinel!=0x110000 or nested_undefined!=0x110201110000) return 9;
    const zero_undefined:[2]u0=@bitCast(@as([2]u0,.{undefined,undefined}));
    if(zero_undefined[0]!=0 or zero_undefined[1]!=0) return 10;
    const whole: [2]u8 = @bitCast(@as(u16,undefined));
    _ = whole;
    return 0;
}
