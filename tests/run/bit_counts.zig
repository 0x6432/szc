// exit: 0
fn check_byte(n: u8) bool {
	var bits = n;
	var ones: u8 = 0;
	var length: u8 = 0;
	while (bits != 0) {
		ones += bits & 1;
		length += 1;
		bits >>= 1;
	}
	bits = n;
	var trailing: u8 = 0;
	if (bits == 0) {
		trailing = 8;
	} else {
		while (bits & 1 == 0) {
			trailing += 1;
			bits >>= 1;
		}
	}
	const signed: i8 = @bitCast(n);
	return @popCount(n) == ones and @clz(n) == 8 - length and @ctz(n) == trailing
		and @popCount(signed) == ones and @clz(signed) == 8 - length and @ctz(signed) == trailing;
}
fn count_u0(n: u0, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u1(n: u1, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i1(n: i1, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u2(n: u2, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i2(n: i2, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u3(n: u3, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i3(n: i3, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u4(n: u4, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i4(n: i4, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u5(n: u5, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i5(n: i5, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u6(n: u6, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i6(n: i6, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u7(n: u7, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i7(n: i7, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u8(n: u8, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i8(n: i8, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u9(n: u9, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i9(n: i9, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u10(n: u10, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i10(n: i10, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u11(n: u11, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i11(n: i11, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u12(n: u12, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i12(n: i12, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u13(n: u13, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i13(n: i13, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u14(n: u14, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i14(n: i14, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u15(n: u15, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i15(n: i15, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u16(n: u16, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i16(n: i16, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u17(n: u17, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i17(n: i17, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u18(n: u18, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i18(n: i18, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u19(n: u19, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i19(n: i19, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u20(n: u20, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i20(n: i20, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u21(n: u21, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i21(n: i21, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u22(n: u22, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i22(n: i22, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u23(n: u23, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i23(n: i23, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u24(n: u24, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i24(n: i24, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u25(n: u25, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i25(n: i25, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u26(n: u26, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i26(n: i26, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u27(n: u27, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i27(n: i27, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u28(n: u28, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i28(n: i28, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u29(n: u29, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i29(n: i29, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u30(n: u30, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i30(n: i30, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u31(n: u31, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i31(n: i31, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u32(n: u32, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i32(n: i32, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u33(n: u33, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i33(n: i33, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u34(n: u34, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i34(n: i34, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u35(n: u35, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i35(n: i35, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u36(n: u36, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i36(n: i36, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u37(n: u37, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i37(n: i37, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u38(n: u38, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i38(n: i38, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u39(n: u39, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i39(n: i39, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u40(n: u40, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i40(n: i40, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u41(n: u41, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i41(n: i41, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u42(n: u42, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i42(n: i42, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u43(n: u43, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i43(n: i43, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u44(n: u44, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i44(n: i44, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u45(n: u45, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i45(n: i45, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u46(n: u46, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i46(n: i46, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u47(n: u47, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i47(n: i47, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u48(n: u48, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i48(n: i48, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u49(n: u49, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i49(n: i49, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u50(n: u50, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i50(n: i50, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u51(n: u51, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i51(n: i51, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u52(n: u52, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i52(n: i52, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u53(n: u53, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i53(n: i53, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u54(n: u54, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i54(n: i54, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u55(n: u55, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i55(n: i55, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u56(n: u56, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i56(n: i56, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u57(n: u57, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i57(n: i57, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u58(n: u58, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i58(n: i58, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u59(n: u59, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i59(n: i59, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u60(n: u60, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i60(n: i60, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u61(n: u61, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i61(n: i61, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u62(n: u62, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i62(n: i62, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u63(n: u63, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i63(n: i63, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_u64(n: u64, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn count_i64(n: i64, a: u8, b: u8, c: u8) bool {
	return @clz(n) == a and @ctz(n) == b and @popCount(n) == c;
}
fn advance(n: *u8) u8 {
	n.* += 1;
	return 128;
}
fn zero(n: *u8) u0 {
	n.* += 1;
	return 0;
}
pub fn main() u8 {
	var n: u16 = 0;
	while (n < 256) : (n += 1)
		if (!check_byte(@intCast(n))) return 1;
	if (!count_u0(0, 0, 0, 0)) return 2;
	if (!count_u1(0, 1, 1, 0)) return 2;
	if (!count_u1(1, 0, 0, 1)) return 2;
	if (!count_i1(0, 1, 1, 0)) return 2;
	if (!count_i1(-1, 0, 0, 1)) return 2;
	if (!count_u2(0, 2, 2, 0)) return 2;
	if (!count_u2(1, 1, 0, 1)) return 2;
	if (!count_u2(3, 0, 0, 2)) return 2;
	if (!count_u2(2, 0, 1, 1)) return 2;
	if (!count_i2(0, 2, 2, 0)) return 2;
	if (!count_i2(1, 1, 0, 1)) return 2;
	if (!count_i2(-1, 0, 0, 2)) return 2;
	if (!count_i2(-2, 0, 1, 1)) return 2;
	if (!count_u3(0, 3, 3, 0)) return 2;
	if (!count_u3(1, 2, 0, 1)) return 2;
	if (!count_u3(7, 0, 0, 3)) return 2;
	if (!count_u3(4, 0, 2, 1)) return 2;
	if (!count_i3(0, 3, 3, 0)) return 2;
	if (!count_i3(1, 2, 0, 1)) return 2;
	if (!count_i3(-1, 0, 0, 3)) return 2;
	if (!count_i3(-4, 0, 2, 1)) return 2;
	if (!count_u4(0, 4, 4, 0)) return 2;
	if (!count_u4(1, 3, 0, 1)) return 2;
	if (!count_u4(15, 0, 0, 4)) return 2;
	if (!count_u4(8, 0, 3, 1)) return 2;
	if (!count_i4(0, 4, 4, 0)) return 2;
	if (!count_i4(1, 3, 0, 1)) return 2;
	if (!count_i4(-1, 0, 0, 4)) return 2;
	if (!count_i4(-8, 0, 3, 1)) return 2;
	if (!count_u5(0, 5, 5, 0)) return 2;
	if (!count_u5(1, 4, 0, 1)) return 2;
	if (!count_u5(31, 0, 0, 5)) return 2;
	if (!count_u5(16, 0, 4, 1)) return 2;
	if (!count_i5(0, 5, 5, 0)) return 2;
	if (!count_i5(1, 4, 0, 1)) return 2;
	if (!count_i5(-1, 0, 0, 5)) return 2;
	if (!count_i5(-16, 0, 4, 1)) return 2;
	if (!count_u6(0, 6, 6, 0)) return 2;
	if (!count_u6(1, 5, 0, 1)) return 2;
	if (!count_u6(63, 0, 0, 6)) return 2;
	if (!count_u6(32, 0, 5, 1)) return 2;
	if (!count_i6(0, 6, 6, 0)) return 2;
	if (!count_i6(1, 5, 0, 1)) return 2;
	if (!count_i6(-1, 0, 0, 6)) return 2;
	if (!count_i6(-32, 0, 5, 1)) return 2;
	if (!count_u7(0, 7, 7, 0)) return 2;
	if (!count_u7(1, 6, 0, 1)) return 2;
	if (!count_u7(127, 0, 0, 7)) return 2;
	if (!count_u7(64, 0, 6, 1)) return 2;
	if (!count_i7(0, 7, 7, 0)) return 2;
	if (!count_i7(1, 6, 0, 1)) return 2;
	if (!count_i7(-1, 0, 0, 7)) return 2;
	if (!count_i7(-64, 0, 6, 1)) return 2;
	if (!count_u8(0, 8, 8, 0)) return 2;
	if (!count_u8(1, 7, 0, 1)) return 2;
	if (!count_u8(255, 0, 0, 8)) return 2;
	if (!count_u8(128, 0, 7, 1)) return 2;
	if (!count_i8(0, 8, 8, 0)) return 2;
	if (!count_i8(1, 7, 0, 1)) return 2;
	if (!count_i8(-1, 0, 0, 8)) return 2;
	if (!count_i8(-128, 0, 7, 1)) return 2;
	if (!count_u9(0, 9, 9, 0)) return 2;
	if (!count_u9(1, 8, 0, 1)) return 2;
	if (!count_u9(511, 0, 0, 9)) return 2;
	if (!count_u9(256, 0, 8, 1)) return 2;
	if (!count_i9(0, 9, 9, 0)) return 2;
	if (!count_i9(1, 8, 0, 1)) return 2;
	if (!count_i9(-1, 0, 0, 9)) return 2;
	if (!count_i9(-256, 0, 8, 1)) return 2;
	if (!count_u10(0, 10, 10, 0)) return 2;
	if (!count_u10(1, 9, 0, 1)) return 2;
	if (!count_u10(1023, 0, 0, 10)) return 2;
	if (!count_u10(512, 0, 9, 1)) return 2;
	if (!count_i10(0, 10, 10, 0)) return 2;
	if (!count_i10(1, 9, 0, 1)) return 2;
	if (!count_i10(-1, 0, 0, 10)) return 2;
	if (!count_i10(-512, 0, 9, 1)) return 2;
	if (!count_u11(0, 11, 11, 0)) return 2;
	if (!count_u11(1, 10, 0, 1)) return 2;
	if (!count_u11(2047, 0, 0, 11)) return 2;
	if (!count_u11(1024, 0, 10, 1)) return 2;
	if (!count_i11(0, 11, 11, 0)) return 2;
	if (!count_i11(1, 10, 0, 1)) return 2;
	if (!count_i11(-1, 0, 0, 11)) return 2;
	if (!count_i11(-1024, 0, 10, 1)) return 2;
	if (!count_u12(0, 12, 12, 0)) return 2;
	if (!count_u12(1, 11, 0, 1)) return 2;
	if (!count_u12(4095, 0, 0, 12)) return 2;
	if (!count_u12(2048, 0, 11, 1)) return 2;
	if (!count_i12(0, 12, 12, 0)) return 2;
	if (!count_i12(1, 11, 0, 1)) return 2;
	if (!count_i12(-1, 0, 0, 12)) return 2;
	if (!count_i12(-2048, 0, 11, 1)) return 2;
	if (!count_u13(0, 13, 13, 0)) return 2;
	if (!count_u13(1, 12, 0, 1)) return 2;
	if (!count_u13(8191, 0, 0, 13)) return 2;
	if (!count_u13(4096, 0, 12, 1)) return 2;
	if (!count_i13(0, 13, 13, 0)) return 2;
	if (!count_i13(1, 12, 0, 1)) return 2;
	if (!count_i13(-1, 0, 0, 13)) return 2;
	if (!count_i13(-4096, 0, 12, 1)) return 2;
	if (!count_u14(0, 14, 14, 0)) return 2;
	if (!count_u14(1, 13, 0, 1)) return 2;
	if (!count_u14(16383, 0, 0, 14)) return 2;
	if (!count_u14(8192, 0, 13, 1)) return 2;
	if (!count_i14(0, 14, 14, 0)) return 2;
	if (!count_i14(1, 13, 0, 1)) return 2;
	if (!count_i14(-1, 0, 0, 14)) return 2;
	if (!count_i14(-8192, 0, 13, 1)) return 2;
	if (!count_u15(0, 15, 15, 0)) return 2;
	if (!count_u15(1, 14, 0, 1)) return 2;
	if (!count_u15(32767, 0, 0, 15)) return 2;
	if (!count_u15(16384, 0, 14, 1)) return 2;
	if (!count_i15(0, 15, 15, 0)) return 2;
	if (!count_i15(1, 14, 0, 1)) return 2;
	if (!count_i15(-1, 0, 0, 15)) return 2;
	if (!count_i15(-16384, 0, 14, 1)) return 2;
	if (!count_u16(0, 16, 16, 0)) return 2;
	if (!count_u16(1, 15, 0, 1)) return 2;
	if (!count_u16(65535, 0, 0, 16)) return 2;
	if (!count_u16(32768, 0, 15, 1)) return 2;
	if (!count_i16(0, 16, 16, 0)) return 2;
	if (!count_i16(1, 15, 0, 1)) return 2;
	if (!count_i16(-1, 0, 0, 16)) return 2;
	if (!count_i16(-32768, 0, 15, 1)) return 2;
	if (!count_u17(0, 17, 17, 0)) return 2;
	if (!count_u17(1, 16, 0, 1)) return 2;
	if (!count_u17(131071, 0, 0, 17)) return 2;
	if (!count_u17(65536, 0, 16, 1)) return 2;
	if (!count_i17(0, 17, 17, 0)) return 2;
	if (!count_i17(1, 16, 0, 1)) return 2;
	if (!count_i17(-1, 0, 0, 17)) return 2;
	if (!count_i17(-65536, 0, 16, 1)) return 2;
	if (!count_u18(0, 18, 18, 0)) return 2;
	if (!count_u18(1, 17, 0, 1)) return 2;
	if (!count_u18(262143, 0, 0, 18)) return 2;
	if (!count_u18(131072, 0, 17, 1)) return 2;
	if (!count_i18(0, 18, 18, 0)) return 2;
	if (!count_i18(1, 17, 0, 1)) return 2;
	if (!count_i18(-1, 0, 0, 18)) return 2;
	if (!count_i18(-131072, 0, 17, 1)) return 2;
	if (!count_u19(0, 19, 19, 0)) return 2;
	if (!count_u19(1, 18, 0, 1)) return 2;
	if (!count_u19(524287, 0, 0, 19)) return 2;
	if (!count_u19(262144, 0, 18, 1)) return 2;
	if (!count_i19(0, 19, 19, 0)) return 2;
	if (!count_i19(1, 18, 0, 1)) return 2;
	if (!count_i19(-1, 0, 0, 19)) return 2;
	if (!count_i19(-262144, 0, 18, 1)) return 2;
	if (!count_u20(0, 20, 20, 0)) return 2;
	if (!count_u20(1, 19, 0, 1)) return 2;
	if (!count_u20(1048575, 0, 0, 20)) return 2;
	if (!count_u20(524288, 0, 19, 1)) return 2;
	if (!count_i20(0, 20, 20, 0)) return 2;
	if (!count_i20(1, 19, 0, 1)) return 2;
	if (!count_i20(-1, 0, 0, 20)) return 2;
	if (!count_i20(-524288, 0, 19, 1)) return 2;
	if (!count_u21(0, 21, 21, 0)) return 2;
	if (!count_u21(1, 20, 0, 1)) return 2;
	if (!count_u21(2097151, 0, 0, 21)) return 2;
	if (!count_u21(1048576, 0, 20, 1)) return 2;
	if (!count_i21(0, 21, 21, 0)) return 2;
	if (!count_i21(1, 20, 0, 1)) return 2;
	if (!count_i21(-1, 0, 0, 21)) return 2;
	if (!count_i21(-1048576, 0, 20, 1)) return 2;
	if (!count_u22(0, 22, 22, 0)) return 2;
	if (!count_u22(1, 21, 0, 1)) return 2;
	if (!count_u22(4194303, 0, 0, 22)) return 2;
	if (!count_u22(2097152, 0, 21, 1)) return 2;
	if (!count_i22(0, 22, 22, 0)) return 2;
	if (!count_i22(1, 21, 0, 1)) return 2;
	if (!count_i22(-1, 0, 0, 22)) return 2;
	if (!count_i22(-2097152, 0, 21, 1)) return 2;
	if (!count_u23(0, 23, 23, 0)) return 2;
	if (!count_u23(1, 22, 0, 1)) return 2;
	if (!count_u23(8388607, 0, 0, 23)) return 2;
	if (!count_u23(4194304, 0, 22, 1)) return 2;
	if (!count_i23(0, 23, 23, 0)) return 2;
	if (!count_i23(1, 22, 0, 1)) return 2;
	if (!count_i23(-1, 0, 0, 23)) return 2;
	if (!count_i23(-4194304, 0, 22, 1)) return 2;
	if (!count_u24(0, 24, 24, 0)) return 2;
	if (!count_u24(1, 23, 0, 1)) return 2;
	if (!count_u24(16777215, 0, 0, 24)) return 2;
	if (!count_u24(8388608, 0, 23, 1)) return 2;
	if (!count_i24(0, 24, 24, 0)) return 2;
	if (!count_i24(1, 23, 0, 1)) return 2;
	if (!count_i24(-1, 0, 0, 24)) return 2;
	if (!count_i24(-8388608, 0, 23, 1)) return 2;
	if (!count_u25(0, 25, 25, 0)) return 2;
	if (!count_u25(1, 24, 0, 1)) return 2;
	if (!count_u25(33554431, 0, 0, 25)) return 2;
	if (!count_u25(16777216, 0, 24, 1)) return 2;
	if (!count_i25(0, 25, 25, 0)) return 2;
	if (!count_i25(1, 24, 0, 1)) return 2;
	if (!count_i25(-1, 0, 0, 25)) return 2;
	if (!count_i25(-16777216, 0, 24, 1)) return 2;
	if (!count_u26(0, 26, 26, 0)) return 2;
	if (!count_u26(1, 25, 0, 1)) return 2;
	if (!count_u26(67108863, 0, 0, 26)) return 2;
	if (!count_u26(33554432, 0, 25, 1)) return 2;
	if (!count_i26(0, 26, 26, 0)) return 2;
	if (!count_i26(1, 25, 0, 1)) return 2;
	if (!count_i26(-1, 0, 0, 26)) return 2;
	if (!count_i26(-33554432, 0, 25, 1)) return 2;
	if (!count_u27(0, 27, 27, 0)) return 2;
	if (!count_u27(1, 26, 0, 1)) return 2;
	if (!count_u27(134217727, 0, 0, 27)) return 2;
	if (!count_u27(67108864, 0, 26, 1)) return 2;
	if (!count_i27(0, 27, 27, 0)) return 2;
	if (!count_i27(1, 26, 0, 1)) return 2;
	if (!count_i27(-1, 0, 0, 27)) return 2;
	if (!count_i27(-67108864, 0, 26, 1)) return 2;
	if (!count_u28(0, 28, 28, 0)) return 2;
	if (!count_u28(1, 27, 0, 1)) return 2;
	if (!count_u28(268435455, 0, 0, 28)) return 2;
	if (!count_u28(134217728, 0, 27, 1)) return 2;
	if (!count_i28(0, 28, 28, 0)) return 2;
	if (!count_i28(1, 27, 0, 1)) return 2;
	if (!count_i28(-1, 0, 0, 28)) return 2;
	if (!count_i28(-134217728, 0, 27, 1)) return 2;
	if (!count_u29(0, 29, 29, 0)) return 2;
	if (!count_u29(1, 28, 0, 1)) return 2;
	if (!count_u29(536870911, 0, 0, 29)) return 2;
	if (!count_u29(268435456, 0, 28, 1)) return 2;
	if (!count_i29(0, 29, 29, 0)) return 2;
	if (!count_i29(1, 28, 0, 1)) return 2;
	if (!count_i29(-1, 0, 0, 29)) return 2;
	if (!count_i29(-268435456, 0, 28, 1)) return 2;
	if (!count_u30(0, 30, 30, 0)) return 2;
	if (!count_u30(1, 29, 0, 1)) return 2;
	if (!count_u30(1073741823, 0, 0, 30)) return 2;
	if (!count_u30(536870912, 0, 29, 1)) return 2;
	if (!count_i30(0, 30, 30, 0)) return 2;
	if (!count_i30(1, 29, 0, 1)) return 2;
	if (!count_i30(-1, 0, 0, 30)) return 2;
	if (!count_i30(-536870912, 0, 29, 1)) return 2;
	if (!count_u31(0, 31, 31, 0)) return 2;
	if (!count_u31(1, 30, 0, 1)) return 2;
	if (!count_u31(2147483647, 0, 0, 31)) return 2;
	if (!count_u31(1073741824, 0, 30, 1)) return 2;
	if (!count_i31(0, 31, 31, 0)) return 2;
	if (!count_i31(1, 30, 0, 1)) return 2;
	if (!count_i31(-1, 0, 0, 31)) return 2;
	if (!count_i31(-1073741824, 0, 30, 1)) return 2;
	if (!count_u32(0, 32, 32, 0)) return 2;
	if (!count_u32(1, 31, 0, 1)) return 2;
	if (!count_u32(4294967295, 0, 0, 32)) return 2;
	if (!count_u32(2147483648, 0, 31, 1)) return 2;
	if (!count_i32(0, 32, 32, 0)) return 2;
	if (!count_i32(1, 31, 0, 1)) return 2;
	if (!count_i32(-1, 0, 0, 32)) return 2;
	if (!count_i32(-2147483648, 0, 31, 1)) return 2;
	if (!count_u33(0, 33, 33, 0)) return 2;
	if (!count_u33(1, 32, 0, 1)) return 2;
	if (!count_u33(8589934591, 0, 0, 33)) return 2;
	if (!count_u33(4294967296, 0, 32, 1)) return 2;
	if (!count_i33(0, 33, 33, 0)) return 2;
	if (!count_i33(1, 32, 0, 1)) return 2;
	if (!count_i33(-1, 0, 0, 33)) return 2;
	if (!count_i33(-4294967296, 0, 32, 1)) return 2;
	if (!count_u34(0, 34, 34, 0)) return 2;
	if (!count_u34(1, 33, 0, 1)) return 2;
	if (!count_u34(17179869183, 0, 0, 34)) return 2;
	if (!count_u34(8589934592, 0, 33, 1)) return 2;
	if (!count_i34(0, 34, 34, 0)) return 2;
	if (!count_i34(1, 33, 0, 1)) return 2;
	if (!count_i34(-1, 0, 0, 34)) return 2;
	if (!count_i34(-8589934592, 0, 33, 1)) return 2;
	if (!count_u35(0, 35, 35, 0)) return 2;
	if (!count_u35(1, 34, 0, 1)) return 2;
	if (!count_u35(34359738367, 0, 0, 35)) return 2;
	if (!count_u35(17179869184, 0, 34, 1)) return 2;
	if (!count_i35(0, 35, 35, 0)) return 2;
	if (!count_i35(1, 34, 0, 1)) return 2;
	if (!count_i35(-1, 0, 0, 35)) return 2;
	if (!count_i35(-17179869184, 0, 34, 1)) return 2;
	if (!count_u36(0, 36, 36, 0)) return 2;
	if (!count_u36(1, 35, 0, 1)) return 2;
	if (!count_u36(68719476735, 0, 0, 36)) return 2;
	if (!count_u36(34359738368, 0, 35, 1)) return 2;
	if (!count_i36(0, 36, 36, 0)) return 2;
	if (!count_i36(1, 35, 0, 1)) return 2;
	if (!count_i36(-1, 0, 0, 36)) return 2;
	if (!count_i36(-34359738368, 0, 35, 1)) return 2;
	if (!count_u37(0, 37, 37, 0)) return 2;
	if (!count_u37(1, 36, 0, 1)) return 2;
	if (!count_u37(137438953471, 0, 0, 37)) return 2;
	if (!count_u37(68719476736, 0, 36, 1)) return 2;
	if (!count_i37(0, 37, 37, 0)) return 2;
	if (!count_i37(1, 36, 0, 1)) return 2;
	if (!count_i37(-1, 0, 0, 37)) return 2;
	if (!count_i37(-68719476736, 0, 36, 1)) return 2;
	if (!count_u38(0, 38, 38, 0)) return 2;
	if (!count_u38(1, 37, 0, 1)) return 2;
	if (!count_u38(274877906943, 0, 0, 38)) return 2;
	if (!count_u38(137438953472, 0, 37, 1)) return 2;
	if (!count_i38(0, 38, 38, 0)) return 2;
	if (!count_i38(1, 37, 0, 1)) return 2;
	if (!count_i38(-1, 0, 0, 38)) return 2;
	if (!count_i38(-137438953472, 0, 37, 1)) return 2;
	if (!count_u39(0, 39, 39, 0)) return 2;
	if (!count_u39(1, 38, 0, 1)) return 2;
	if (!count_u39(549755813887, 0, 0, 39)) return 2;
	if (!count_u39(274877906944, 0, 38, 1)) return 2;
	if (!count_i39(0, 39, 39, 0)) return 2;
	if (!count_i39(1, 38, 0, 1)) return 2;
	if (!count_i39(-1, 0, 0, 39)) return 2;
	if (!count_i39(-274877906944, 0, 38, 1)) return 2;
	if (!count_u40(0, 40, 40, 0)) return 2;
	if (!count_u40(1, 39, 0, 1)) return 2;
	if (!count_u40(1099511627775, 0, 0, 40)) return 2;
	if (!count_u40(549755813888, 0, 39, 1)) return 2;
	if (!count_i40(0, 40, 40, 0)) return 2;
	if (!count_i40(1, 39, 0, 1)) return 2;
	if (!count_i40(-1, 0, 0, 40)) return 2;
	if (!count_i40(-549755813888, 0, 39, 1)) return 2;
	if (!count_u41(0, 41, 41, 0)) return 2;
	if (!count_u41(1, 40, 0, 1)) return 2;
	if (!count_u41(2199023255551, 0, 0, 41)) return 2;
	if (!count_u41(1099511627776, 0, 40, 1)) return 2;
	if (!count_i41(0, 41, 41, 0)) return 2;
	if (!count_i41(1, 40, 0, 1)) return 2;
	if (!count_i41(-1, 0, 0, 41)) return 2;
	if (!count_i41(-1099511627776, 0, 40, 1)) return 2;
	if (!count_u42(0, 42, 42, 0)) return 2;
	if (!count_u42(1, 41, 0, 1)) return 2;
	if (!count_u42(4398046511103, 0, 0, 42)) return 2;
	if (!count_u42(2199023255552, 0, 41, 1)) return 2;
	if (!count_i42(0, 42, 42, 0)) return 2;
	if (!count_i42(1, 41, 0, 1)) return 2;
	if (!count_i42(-1, 0, 0, 42)) return 2;
	if (!count_i42(-2199023255552, 0, 41, 1)) return 2;
	if (!count_u43(0, 43, 43, 0)) return 2;
	if (!count_u43(1, 42, 0, 1)) return 2;
	if (!count_u43(8796093022207, 0, 0, 43)) return 2;
	if (!count_u43(4398046511104, 0, 42, 1)) return 2;
	if (!count_i43(0, 43, 43, 0)) return 2;
	if (!count_i43(1, 42, 0, 1)) return 2;
	if (!count_i43(-1, 0, 0, 43)) return 2;
	if (!count_i43(-4398046511104, 0, 42, 1)) return 2;
	if (!count_u44(0, 44, 44, 0)) return 2;
	if (!count_u44(1, 43, 0, 1)) return 2;
	if (!count_u44(17592186044415, 0, 0, 44)) return 2;
	if (!count_u44(8796093022208, 0, 43, 1)) return 2;
	if (!count_i44(0, 44, 44, 0)) return 2;
	if (!count_i44(1, 43, 0, 1)) return 2;
	if (!count_i44(-1, 0, 0, 44)) return 2;
	if (!count_i44(-8796093022208, 0, 43, 1)) return 2;
	if (!count_u45(0, 45, 45, 0)) return 2;
	if (!count_u45(1, 44, 0, 1)) return 2;
	if (!count_u45(35184372088831, 0, 0, 45)) return 2;
	if (!count_u45(17592186044416, 0, 44, 1)) return 2;
	if (!count_i45(0, 45, 45, 0)) return 2;
	if (!count_i45(1, 44, 0, 1)) return 2;
	if (!count_i45(-1, 0, 0, 45)) return 2;
	if (!count_i45(-17592186044416, 0, 44, 1)) return 2;
	if (!count_u46(0, 46, 46, 0)) return 2;
	if (!count_u46(1, 45, 0, 1)) return 2;
	if (!count_u46(70368744177663, 0, 0, 46)) return 2;
	if (!count_u46(35184372088832, 0, 45, 1)) return 2;
	if (!count_i46(0, 46, 46, 0)) return 2;
	if (!count_i46(1, 45, 0, 1)) return 2;
	if (!count_i46(-1, 0, 0, 46)) return 2;
	if (!count_i46(-35184372088832, 0, 45, 1)) return 2;
	if (!count_u47(0, 47, 47, 0)) return 2;
	if (!count_u47(1, 46, 0, 1)) return 2;
	if (!count_u47(140737488355327, 0, 0, 47)) return 2;
	if (!count_u47(70368744177664, 0, 46, 1)) return 2;
	if (!count_i47(0, 47, 47, 0)) return 2;
	if (!count_i47(1, 46, 0, 1)) return 2;
	if (!count_i47(-1, 0, 0, 47)) return 2;
	if (!count_i47(-70368744177664, 0, 46, 1)) return 2;
	if (!count_u48(0, 48, 48, 0)) return 2;
	if (!count_u48(1, 47, 0, 1)) return 2;
	if (!count_u48(281474976710655, 0, 0, 48)) return 2;
	if (!count_u48(140737488355328, 0, 47, 1)) return 2;
	if (!count_i48(0, 48, 48, 0)) return 2;
	if (!count_i48(1, 47, 0, 1)) return 2;
	if (!count_i48(-1, 0, 0, 48)) return 2;
	if (!count_i48(-140737488355328, 0, 47, 1)) return 2;
	if (!count_u49(0, 49, 49, 0)) return 2;
	if (!count_u49(1, 48, 0, 1)) return 2;
	if (!count_u49(562949953421311, 0, 0, 49)) return 2;
	if (!count_u49(281474976710656, 0, 48, 1)) return 2;
	if (!count_i49(0, 49, 49, 0)) return 2;
	if (!count_i49(1, 48, 0, 1)) return 2;
	if (!count_i49(-1, 0, 0, 49)) return 2;
	if (!count_i49(-281474976710656, 0, 48, 1)) return 2;
	if (!count_u50(0, 50, 50, 0)) return 2;
	if (!count_u50(1, 49, 0, 1)) return 2;
	if (!count_u50(1125899906842623, 0, 0, 50)) return 2;
	if (!count_u50(562949953421312, 0, 49, 1)) return 2;
	if (!count_i50(0, 50, 50, 0)) return 2;
	if (!count_i50(1, 49, 0, 1)) return 2;
	if (!count_i50(-1, 0, 0, 50)) return 2;
	if (!count_i50(-562949953421312, 0, 49, 1)) return 2;
	if (!count_u51(0, 51, 51, 0)) return 2;
	if (!count_u51(1, 50, 0, 1)) return 2;
	if (!count_u51(2251799813685247, 0, 0, 51)) return 2;
	if (!count_u51(1125899906842624, 0, 50, 1)) return 2;
	if (!count_i51(0, 51, 51, 0)) return 2;
	if (!count_i51(1, 50, 0, 1)) return 2;
	if (!count_i51(-1, 0, 0, 51)) return 2;
	if (!count_i51(-1125899906842624, 0, 50, 1)) return 2;
	if (!count_u52(0, 52, 52, 0)) return 2;
	if (!count_u52(1, 51, 0, 1)) return 2;
	if (!count_u52(4503599627370495, 0, 0, 52)) return 2;
	if (!count_u52(2251799813685248, 0, 51, 1)) return 2;
	if (!count_i52(0, 52, 52, 0)) return 2;
	if (!count_i52(1, 51, 0, 1)) return 2;
	if (!count_i52(-1, 0, 0, 52)) return 2;
	if (!count_i52(-2251799813685248, 0, 51, 1)) return 2;
	if (!count_u53(0, 53, 53, 0)) return 2;
	if (!count_u53(1, 52, 0, 1)) return 2;
	if (!count_u53(9007199254740991, 0, 0, 53)) return 2;
	if (!count_u53(4503599627370496, 0, 52, 1)) return 2;
	if (!count_i53(0, 53, 53, 0)) return 2;
	if (!count_i53(1, 52, 0, 1)) return 2;
	if (!count_i53(-1, 0, 0, 53)) return 2;
	if (!count_i53(-4503599627370496, 0, 52, 1)) return 2;
	if (!count_u54(0, 54, 54, 0)) return 2;
	if (!count_u54(1, 53, 0, 1)) return 2;
	if (!count_u54(18014398509481983, 0, 0, 54)) return 2;
	if (!count_u54(9007199254740992, 0, 53, 1)) return 2;
	if (!count_i54(0, 54, 54, 0)) return 2;
	if (!count_i54(1, 53, 0, 1)) return 2;
	if (!count_i54(-1, 0, 0, 54)) return 2;
	if (!count_i54(-9007199254740992, 0, 53, 1)) return 2;
	if (!count_u55(0, 55, 55, 0)) return 2;
	if (!count_u55(1, 54, 0, 1)) return 2;
	if (!count_u55(36028797018963967, 0, 0, 55)) return 2;
	if (!count_u55(18014398509481984, 0, 54, 1)) return 2;
	if (!count_i55(0, 55, 55, 0)) return 2;
	if (!count_i55(1, 54, 0, 1)) return 2;
	if (!count_i55(-1, 0, 0, 55)) return 2;
	if (!count_i55(-18014398509481984, 0, 54, 1)) return 2;
	if (!count_u56(0, 56, 56, 0)) return 2;
	if (!count_u56(1, 55, 0, 1)) return 2;
	if (!count_u56(72057594037927935, 0, 0, 56)) return 2;
	if (!count_u56(36028797018963968, 0, 55, 1)) return 2;
	if (!count_i56(0, 56, 56, 0)) return 2;
	if (!count_i56(1, 55, 0, 1)) return 2;
	if (!count_i56(-1, 0, 0, 56)) return 2;
	if (!count_i56(-36028797018963968, 0, 55, 1)) return 2;
	if (!count_u57(0, 57, 57, 0)) return 2;
	if (!count_u57(1, 56, 0, 1)) return 2;
	if (!count_u57(144115188075855871, 0, 0, 57)) return 2;
	if (!count_u57(72057594037927936, 0, 56, 1)) return 2;
	if (!count_i57(0, 57, 57, 0)) return 2;
	if (!count_i57(1, 56, 0, 1)) return 2;
	if (!count_i57(-1, 0, 0, 57)) return 2;
	if (!count_i57(-72057594037927936, 0, 56, 1)) return 2;
	if (!count_u58(0, 58, 58, 0)) return 2;
	if (!count_u58(1, 57, 0, 1)) return 2;
	if (!count_u58(288230376151711743, 0, 0, 58)) return 2;
	if (!count_u58(144115188075855872, 0, 57, 1)) return 2;
	if (!count_i58(0, 58, 58, 0)) return 2;
	if (!count_i58(1, 57, 0, 1)) return 2;
	if (!count_i58(-1, 0, 0, 58)) return 2;
	if (!count_i58(-144115188075855872, 0, 57, 1)) return 2;
	if (!count_u59(0, 59, 59, 0)) return 2;
	if (!count_u59(1, 58, 0, 1)) return 2;
	if (!count_u59(576460752303423487, 0, 0, 59)) return 2;
	if (!count_u59(288230376151711744, 0, 58, 1)) return 2;
	if (!count_i59(0, 59, 59, 0)) return 2;
	if (!count_i59(1, 58, 0, 1)) return 2;
	if (!count_i59(-1, 0, 0, 59)) return 2;
	if (!count_i59(-288230376151711744, 0, 58, 1)) return 2;
	if (!count_u60(0, 60, 60, 0)) return 2;
	if (!count_u60(1, 59, 0, 1)) return 2;
	if (!count_u60(1152921504606846975, 0, 0, 60)) return 2;
	if (!count_u60(576460752303423488, 0, 59, 1)) return 2;
	if (!count_i60(0, 60, 60, 0)) return 2;
	if (!count_i60(1, 59, 0, 1)) return 2;
	if (!count_i60(-1, 0, 0, 60)) return 2;
	if (!count_i60(-576460752303423488, 0, 59, 1)) return 2;
	if (!count_u61(0, 61, 61, 0)) return 2;
	if (!count_u61(1, 60, 0, 1)) return 2;
	if (!count_u61(2305843009213693951, 0, 0, 61)) return 2;
	if (!count_u61(1152921504606846976, 0, 60, 1)) return 2;
	if (!count_i61(0, 61, 61, 0)) return 2;
	if (!count_i61(1, 60, 0, 1)) return 2;
	if (!count_i61(-1, 0, 0, 61)) return 2;
	if (!count_i61(-1152921504606846976, 0, 60, 1)) return 2;
	if (!count_u62(0, 62, 62, 0)) return 2;
	if (!count_u62(1, 61, 0, 1)) return 2;
	if (!count_u62(4611686018427387903, 0, 0, 62)) return 2;
	if (!count_u62(2305843009213693952, 0, 61, 1)) return 2;
	if (!count_i62(0, 62, 62, 0)) return 2;
	if (!count_i62(1, 61, 0, 1)) return 2;
	if (!count_i62(-1, 0, 0, 62)) return 2;
	if (!count_i62(-2305843009213693952, 0, 61, 1)) return 2;
	if (!count_u63(0, 63, 63, 0)) return 2;
	if (!count_u63(1, 62, 0, 1)) return 2;
	if (!count_u63(9223372036854775807, 0, 0, 63)) return 2;
	if (!count_u63(4611686018427387904, 0, 62, 1)) return 2;
	if (!count_i63(0, 63, 63, 0)) return 2;
	if (!count_i63(1, 62, 0, 1)) return 2;
	if (!count_i63(-1, 0, 0, 63)) return 2;
	if (!count_i63(-4611686018427387904, 0, 62, 1)) return 2;
	if (!count_u64(0, 64, 64, 0)) return 2;
	if (!count_u64(1, 63, 0, 1)) return 2;
	if (!count_u64(18446744073709551615, 0, 0, 64)) return 2;
	if (!count_u64(9223372036854775808, 0, 63, 1)) return 2;
	if (!count_i64(0, 64, 64, 0)) return 2;
	if (!count_i64(1, 63, 0, 1)) return 2;
	if (!count_i64(-1, 0, 0, 64)) return 2;
	if (!count_i64(-9223372036854775808, 0, 63, 1)) return 2;
	var calls: u8 = 0;
	if (@clz(advance(&calls)) != 0 or @ctz(advance(&calls)) != 7 or @popCount(advance(&calls)) != 1 or calls != 3) return 3;
	if (@clz(zero(&calls)) != 0 or @ctz(zero(&calls)) != 0 or @popCount(zero(&calls)) != 0 or calls != 6) return 4;
	return 0;
}
