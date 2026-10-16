// exit: 0
fn check_byte(n: u8) bool {
	var bits = n;
	var reverse: u8 = 0;
	var i: u8 = 0;
	while (i < 8) : (i += 1) {
		reverse = (reverse << 1) | (bits & 1);
		bits >>= 1;
	}
	const signed: i8 = @bitCast(n);
	const expected: i8 = @bitCast(reverse);
	return @bitReverse(n) == reverse and @bitReverse(signed) == expected
		and @byteSwap(n) == n and @byteSwap(signed) == signed;
}
fn bitReverse_u0(n:u0, expected:u0) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u0(n:u0, expected:u0) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u1(n:u1, expected:u1) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i1(n:i1, expected:i1) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u2(n:u2, expected:u2) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i2(n:i2, expected:i2) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u3(n:u3, expected:u3) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i3(n:i3, expected:i3) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u4(n:u4, expected:u4) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i4(n:i4, expected:i4) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u5(n:u5, expected:u5) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i5(n:i5, expected:i5) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u6(n:u6, expected:u6) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i6(n:i6, expected:i6) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u7(n:u7, expected:u7) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i7(n:i7, expected:i7) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u8(n:u8, expected:u8) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u8(n:u8, expected:u8) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i8(n:i8, expected:i8) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i8(n:i8, expected:i8) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u9(n:u9, expected:u9) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i9(n:i9, expected:i9) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u10(n:u10, expected:u10) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i10(n:i10, expected:i10) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u11(n:u11, expected:u11) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i11(n:i11, expected:i11) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u12(n:u12, expected:u12) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i12(n:i12, expected:i12) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u13(n:u13, expected:u13) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i13(n:i13, expected:i13) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u14(n:u14, expected:u14) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i14(n:i14, expected:i14) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u15(n:u15, expected:u15) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i15(n:i15, expected:i15) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u16(n:u16, expected:u16) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u16(n:u16, expected:u16) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i16(n:i16, expected:i16) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i16(n:i16, expected:i16) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u17(n:u17, expected:u17) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i17(n:i17, expected:i17) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u18(n:u18, expected:u18) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i18(n:i18, expected:i18) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u19(n:u19, expected:u19) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i19(n:i19, expected:i19) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u20(n:u20, expected:u20) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i20(n:i20, expected:i20) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u21(n:u21, expected:u21) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i21(n:i21, expected:i21) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u22(n:u22, expected:u22) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i22(n:i22, expected:i22) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u23(n:u23, expected:u23) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i23(n:i23, expected:i23) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u24(n:u24, expected:u24) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u24(n:u24, expected:u24) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i24(n:i24, expected:i24) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i24(n:i24, expected:i24) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u25(n:u25, expected:u25) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i25(n:i25, expected:i25) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u26(n:u26, expected:u26) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i26(n:i26, expected:i26) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u27(n:u27, expected:u27) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i27(n:i27, expected:i27) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u28(n:u28, expected:u28) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i28(n:i28, expected:i28) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u29(n:u29, expected:u29) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i29(n:i29, expected:i29) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u30(n:u30, expected:u30) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i30(n:i30, expected:i30) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u31(n:u31, expected:u31) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i31(n:i31, expected:i31) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u32(n:u32, expected:u32) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u32(n:u32, expected:u32) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i32(n:i32, expected:i32) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i32(n:i32, expected:i32) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u33(n:u33, expected:u33) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i33(n:i33, expected:i33) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u34(n:u34, expected:u34) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i34(n:i34, expected:i34) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u35(n:u35, expected:u35) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i35(n:i35, expected:i35) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u36(n:u36, expected:u36) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i36(n:i36, expected:i36) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u37(n:u37, expected:u37) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i37(n:i37, expected:i37) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u38(n:u38, expected:u38) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i38(n:i38, expected:i38) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u39(n:u39, expected:u39) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i39(n:i39, expected:i39) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u40(n:u40, expected:u40) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u40(n:u40, expected:u40) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i40(n:i40, expected:i40) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i40(n:i40, expected:i40) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u41(n:u41, expected:u41) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i41(n:i41, expected:i41) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u42(n:u42, expected:u42) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i42(n:i42, expected:i42) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u43(n:u43, expected:u43) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i43(n:i43, expected:i43) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u44(n:u44, expected:u44) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i44(n:i44, expected:i44) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u45(n:u45, expected:u45) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i45(n:i45, expected:i45) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u46(n:u46, expected:u46) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i46(n:i46, expected:i46) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u47(n:u47, expected:u47) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i47(n:i47, expected:i47) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u48(n:u48, expected:u48) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u48(n:u48, expected:u48) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i48(n:i48, expected:i48) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i48(n:i48, expected:i48) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u49(n:u49, expected:u49) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i49(n:i49, expected:i49) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u50(n:u50, expected:u50) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i50(n:i50, expected:i50) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u51(n:u51, expected:u51) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i51(n:i51, expected:i51) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u52(n:u52, expected:u52) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i52(n:i52, expected:i52) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u53(n:u53, expected:u53) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i53(n:i53, expected:i53) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u54(n:u54, expected:u54) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i54(n:i54, expected:i54) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u55(n:u55, expected:u55) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i55(n:i55, expected:i55) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u56(n:u56, expected:u56) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u56(n:u56, expected:u56) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i56(n:i56, expected:i56) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i56(n:i56, expected:i56) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_u57(n:u57, expected:u57) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i57(n:i57, expected:i57) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u58(n:u58, expected:u58) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i58(n:i58, expected:i58) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u59(n:u59, expected:u59) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i59(n:i59, expected:i59) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u60(n:u60, expected:u60) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i60(n:i60, expected:i60) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u61(n:u61, expected:u61) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i61(n:i61, expected:i61) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u62(n:u62, expected:u62) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i62(n:i62, expected:i62) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u63(n:u63, expected:u63) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_i63(n:i63, expected:i63) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn bitReverse_u64(n:u64, expected:u64) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_u64(n:u64, expected:u64) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
}
fn bitReverse_i64(n:i64, expected:i64) bool {
	return @bitReverse(n) == expected and @bitReverse(@bitReverse(n)) == n;
}
fn byteSwap_i64(n:i64, expected:i64) bool {
	return @byteSwap(n) == expected and @byteSwap(@byteSwap(n)) == n;
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
	if (!bitReverse_u0(0, 0)) return 2;
	if (!byteSwap_u0(0, 0)) return 2;
	if (!bitReverse_u1(0, 0)) return 2;
	if (!bitReverse_u1(1, 1)) return 2;
	if (!bitReverse_i1(0, 0)) return 2;
	if (!bitReverse_i1(-1, -1)) return 2;
	if (!bitReverse_u2(0, 0)) return 2;
	if (!bitReverse_u2(1, 2)) return 2;
	if (!bitReverse_u2(3, 3)) return 2;
	if (!bitReverse_u2(2, 1)) return 2;
	if (!bitReverse_i2(0, 0)) return 2;
	if (!bitReverse_i2(1, -2)) return 2;
	if (!bitReverse_i2(-1, -1)) return 2;
	if (!bitReverse_i2(-2, 1)) return 2;
	if (!bitReverse_u3(0, 0)) return 2;
	if (!bitReverse_u3(1, 4)) return 2;
	if (!bitReverse_u3(7, 7)) return 2;
	if (!bitReverse_u3(4, 1)) return 2;
	if (!bitReverse_i3(0, 0)) return 2;
	if (!bitReverse_i3(1, -4)) return 2;
	if (!bitReverse_i3(-1, -1)) return 2;
	if (!bitReverse_i3(-4, 1)) return 2;
	if (!bitReverse_u4(0, 0)) return 2;
	if (!bitReverse_u4(1, 8)) return 2;
	if (!bitReverse_u4(15, 15)) return 2;
	if (!bitReverse_u4(8, 1)) return 2;
	if (!bitReverse_i4(0, 0)) return 2;
	if (!bitReverse_i4(1, -8)) return 2;
	if (!bitReverse_i4(-1, -1)) return 2;
	if (!bitReverse_i4(-8, 1)) return 2;
	if (!bitReverse_u5(0, 0)) return 2;
	if (!bitReverse_u5(1, 16)) return 2;
	if (!bitReverse_u5(31, 31)) return 2;
	if (!bitReverse_u5(16, 1)) return 2;
	if (!bitReverse_u5(15, 30)) return 2;
	if (!bitReverse_i5(0, 0)) return 2;
	if (!bitReverse_i5(1, -16)) return 2;
	if (!bitReverse_i5(-1, -1)) return 2;
	if (!bitReverse_i5(-16, 1)) return 2;
	if (!bitReverse_i5(15, -2)) return 2;
	if (!bitReverse_u6(0, 0)) return 2;
	if (!bitReverse_u6(1, 32)) return 2;
	if (!bitReverse_u6(63, 63)) return 2;
	if (!bitReverse_u6(32, 1)) return 2;
	if (!bitReverse_u6(47, 61)) return 2;
	if (!bitReverse_i6(0, 0)) return 2;
	if (!bitReverse_i6(1, -32)) return 2;
	if (!bitReverse_i6(-1, -1)) return 2;
	if (!bitReverse_i6(-32, 1)) return 2;
	if (!bitReverse_i6(-17, -3)) return 2;
	if (!bitReverse_u7(0, 0)) return 2;
	if (!bitReverse_u7(1, 64)) return 2;
	if (!bitReverse_u7(127, 127)) return 2;
	if (!bitReverse_u7(64, 1)) return 2;
	if (!bitReverse_u7(111, 123)) return 2;
	if (!bitReverse_i7(0, 0)) return 2;
	if (!bitReverse_i7(1, -64)) return 2;
	if (!bitReverse_i7(-1, -1)) return 2;
	if (!bitReverse_i7(-64, 1)) return 2;
	if (!bitReverse_i7(-17, -5)) return 2;
	if (!bitReverse_u8(0, 0)) return 2;
	if (!bitReverse_u8(1, 128)) return 2;
	if (!bitReverse_u8(255, 255)) return 2;
	if (!bitReverse_u8(128, 1)) return 2;
	if (!bitReverse_u8(239, 247)) return 2;
	if (!byteSwap_u8(0, 0)) return 2;
	if (!byteSwap_u8(1, 1)) return 2;
	if (!byteSwap_u8(255, 255)) return 2;
	if (!byteSwap_u8(128, 128)) return 2;
	if (!byteSwap_u8(239, 239)) return 2;
	if (!bitReverse_i8(0, 0)) return 2;
	if (!bitReverse_i8(1, -128)) return 2;
	if (!bitReverse_i8(-1, -1)) return 2;
	if (!bitReverse_i8(-128, 1)) return 2;
	if (!bitReverse_i8(-17, -9)) return 2;
	if (!byteSwap_i8(0, 0)) return 2;
	if (!byteSwap_i8(1, 1)) return 2;
	if (!byteSwap_i8(-1, -1)) return 2;
	if (!byteSwap_i8(-128, -128)) return 2;
	if (!byteSwap_i8(-17, -17)) return 2;
	if (!bitReverse_u9(0, 0)) return 2;
	if (!bitReverse_u9(1, 256)) return 2;
	if (!bitReverse_u9(511, 511)) return 2;
	if (!bitReverse_u9(256, 1)) return 2;
	if (!bitReverse_u9(495, 495)) return 2;
	if (!bitReverse_i9(0, 0)) return 2;
	if (!bitReverse_i9(1, -256)) return 2;
	if (!bitReverse_i9(-1, -1)) return 2;
	if (!bitReverse_i9(-256, 1)) return 2;
	if (!bitReverse_i9(-17, -17)) return 2;
	if (!bitReverse_u10(0, 0)) return 2;
	if (!bitReverse_u10(1, 512)) return 2;
	if (!bitReverse_u10(1023, 1023)) return 2;
	if (!bitReverse_u10(512, 1)) return 2;
	if (!bitReverse_u10(495, 990)) return 2;
	if (!bitReverse_i10(0, 0)) return 2;
	if (!bitReverse_i10(1, -512)) return 2;
	if (!bitReverse_i10(-1, -1)) return 2;
	if (!bitReverse_i10(-512, 1)) return 2;
	if (!bitReverse_i10(495, -34)) return 2;
	if (!bitReverse_u11(0, 0)) return 2;
	if (!bitReverse_u11(1, 1024)) return 2;
	if (!bitReverse_u11(2047, 2047)) return 2;
	if (!bitReverse_u11(1024, 1)) return 2;
	if (!bitReverse_u11(1519, 1981)) return 2;
	if (!bitReverse_i11(0, 0)) return 2;
	if (!bitReverse_i11(1, -1024)) return 2;
	if (!bitReverse_i11(-1, -1)) return 2;
	if (!bitReverse_i11(-1024, 1)) return 2;
	if (!bitReverse_i11(-529, -67)) return 2;
	if (!bitReverse_u12(0, 0)) return 2;
	if (!bitReverse_u12(1, 2048)) return 2;
	if (!bitReverse_u12(4095, 4095)) return 2;
	if (!bitReverse_u12(2048, 1)) return 2;
	if (!bitReverse_u12(3567, 3963)) return 2;
	if (!bitReverse_i12(0, 0)) return 2;
	if (!bitReverse_i12(1, -2048)) return 2;
	if (!bitReverse_i12(-1, -1)) return 2;
	if (!bitReverse_i12(-2048, 1)) return 2;
	if (!bitReverse_i12(-529, -133)) return 2;
	if (!bitReverse_u13(0, 0)) return 2;
	if (!bitReverse_u13(1, 4096)) return 2;
	if (!bitReverse_u13(8191, 8191)) return 2;
	if (!bitReverse_u13(4096, 1)) return 2;
	if (!bitReverse_u13(3567, 7926)) return 2;
	if (!bitReverse_i13(0, 0)) return 2;
	if (!bitReverse_i13(1, -4096)) return 2;
	if (!bitReverse_i13(-1, -1)) return 2;
	if (!bitReverse_i13(-4096, 1)) return 2;
	if (!bitReverse_i13(3567, -266)) return 2;
	if (!bitReverse_u14(0, 0)) return 2;
	if (!bitReverse_u14(1, 8192)) return 2;
	if (!bitReverse_u14(16383, 16383)) return 2;
	if (!bitReverse_u14(8192, 1)) return 2;
	if (!bitReverse_u14(3567, 15852)) return 2;
	if (!bitReverse_i14(0, 0)) return 2;
	if (!bitReverse_i14(1, -8192)) return 2;
	if (!bitReverse_i14(-1, -1)) return 2;
	if (!bitReverse_i14(-8192, 1)) return 2;
	if (!bitReverse_i14(3567, -532)) return 2;
	if (!bitReverse_u15(0, 0)) return 2;
	if (!bitReverse_u15(1, 16384)) return 2;
	if (!bitReverse_u15(32767, 32767)) return 2;
	if (!bitReverse_u15(16384, 1)) return 2;
	if (!bitReverse_u15(19951, 31705)) return 2;
	if (!bitReverse_i15(0, 0)) return 2;
	if (!bitReverse_i15(1, -16384)) return 2;
	if (!bitReverse_i15(-1, -1)) return 2;
	if (!bitReverse_i15(-16384, 1)) return 2;
	if (!bitReverse_i15(-12817, -1063)) return 2;
	if (!bitReverse_u16(0, 0)) return 2;
	if (!bitReverse_u16(1, 32768)) return 2;
	if (!bitReverse_u16(65535, 65535)) return 2;
	if (!bitReverse_u16(32768, 1)) return 2;
	if (!bitReverse_u16(52719, 63411)) return 2;
	if (!byteSwap_u16(0, 0)) return 2;
	if (!byteSwap_u16(1, 256)) return 2;
	if (!byteSwap_u16(65535, 65535)) return 2;
	if (!byteSwap_u16(32768, 128)) return 2;
	if (!byteSwap_u16(52719, 61389)) return 2;
	if (!bitReverse_i16(0, 0)) return 2;
	if (!bitReverse_i16(1, -32768)) return 2;
	if (!bitReverse_i16(-1, -1)) return 2;
	if (!bitReverse_i16(-32768, 1)) return 2;
	if (!bitReverse_i16(-12817, -2125)) return 2;
	if (!byteSwap_i16(0, 0)) return 2;
	if (!byteSwap_i16(1, 256)) return 2;
	if (!byteSwap_i16(-1, -1)) return 2;
	if (!byteSwap_i16(-32768, 128)) return 2;
	if (!byteSwap_i16(-12817, -4147)) return 2;
	if (!bitReverse_u17(0, 0)) return 2;
	if (!bitReverse_u17(1, 65536)) return 2;
	if (!bitReverse_u17(131071, 131071)) return 2;
	if (!bitReverse_u17(65536, 1)) return 2;
	if (!bitReverse_u17(118255, 126823)) return 2;
	if (!bitReverse_i17(0, 0)) return 2;
	if (!bitReverse_i17(1, -65536)) return 2;
	if (!bitReverse_i17(-1, -1)) return 2;
	if (!bitReverse_i17(-65536, 1)) return 2;
	if (!bitReverse_i17(-12817, -4249)) return 2;
	if (!bitReverse_u18(0, 0)) return 2;
	if (!bitReverse_u18(1, 131072)) return 2;
	if (!bitReverse_u18(262143, 262143)) return 2;
	if (!bitReverse_u18(131072, 1)) return 2;
	if (!bitReverse_u18(249327, 253647)) return 2;
	if (!bitReverse_i18(0, 0)) return 2;
	if (!bitReverse_i18(1, -131072)) return 2;
	if (!bitReverse_i18(-1, -1)) return 2;
	if (!bitReverse_i18(-131072, 1)) return 2;
	if (!bitReverse_i18(-12817, -8497)) return 2;
	if (!bitReverse_u19(0, 0)) return 2;
	if (!bitReverse_u19(1, 262144)) return 2;
	if (!bitReverse_u19(524287, 524287)) return 2;
	if (!bitReverse_u19(262144, 1)) return 2;
	if (!bitReverse_u19(249327, 507294)) return 2;
	if (!bitReverse_i19(0, 0)) return 2;
	if (!bitReverse_i19(1, -262144)) return 2;
	if (!bitReverse_i19(-1, -1)) return 2;
	if (!bitReverse_i19(-262144, 1)) return 2;
	if (!bitReverse_i19(249327, -16994)) return 2;
	if (!bitReverse_u20(0, 0)) return 2;
	if (!bitReverse_u20(1, 524288)) return 2;
	if (!bitReverse_u20(1048575, 1048575)) return 2;
	if (!bitReverse_u20(524288, 1)) return 2;
	if (!bitReverse_u20(773615, 1014589)) return 2;
	if (!bitReverse_i20(0, 0)) return 2;
	if (!bitReverse_i20(1, -524288)) return 2;
	if (!bitReverse_i20(-1, -1)) return 2;
	if (!bitReverse_i20(-524288, 1)) return 2;
	if (!bitReverse_i20(-274961, -33987)) return 2;
	if (!bitReverse_u21(0, 0)) return 2;
	if (!bitReverse_u21(1, 1048576)) return 2;
	if (!bitReverse_u21(2097151, 2097151)) return 2;
	if (!bitReverse_u21(1048576, 1)) return 2;
	if (!bitReverse_u21(773615, 2029178)) return 2;
	if (!bitReverse_i21(0, 0)) return 2;
	if (!bitReverse_i21(1, -1048576)) return 2;
	if (!bitReverse_i21(-1, -1)) return 2;
	if (!bitReverse_i21(-1048576, 1)) return 2;
	if (!bitReverse_i21(773615, -67974)) return 2;
	if (!bitReverse_u22(0, 0)) return 2;
	if (!bitReverse_u22(1, 2097152)) return 2;
	if (!bitReverse_u22(4194303, 4194303)) return 2;
	if (!bitReverse_u22(2097152, 1)) return 2;
	if (!bitReverse_u22(2870767, 4058357)) return 2;
	if (!bitReverse_i22(0, 0)) return 2;
	if (!bitReverse_i22(1, -2097152)) return 2;
	if (!bitReverse_i22(-1, -1)) return 2;
	if (!bitReverse_i22(-2097152, 1)) return 2;
	if (!bitReverse_i22(-1323537, -135947)) return 2;
	if (!bitReverse_u23(0, 0)) return 2;
	if (!bitReverse_u23(1, 4194304)) return 2;
	if (!bitReverse_u23(8388607, 8388607)) return 2;
	if (!bitReverse_u23(4194304, 1)) return 2;
	if (!bitReverse_u23(2870767, 8116714)) return 2;
	if (!bitReverse_i23(0, 0)) return 2;
	if (!bitReverse_i23(1, -4194304)) return 2;
	if (!bitReverse_i23(-1, -1)) return 2;
	if (!bitReverse_i23(-4194304, 1)) return 2;
	if (!bitReverse_i23(2870767, -271894)) return 2;
	if (!bitReverse_u24(0, 0)) return 2;
	if (!bitReverse_u24(1, 8388608)) return 2;
	if (!bitReverse_u24(16777215, 16777215)) return 2;
	if (!bitReverse_u24(8388608, 1)) return 2;
	if (!bitReverse_u24(11259375, 16233429)) return 2;
	if (!byteSwap_u24(0, 0)) return 2;
	if (!byteSwap_u24(1, 65536)) return 2;
	if (!byteSwap_u24(16777215, 16777215)) return 2;
	if (!byteSwap_u24(8388608, 128)) return 2;
	if (!byteSwap_u24(11259375, 15715755)) return 2;
	if (!bitReverse_i24(0, 0)) return 2;
	if (!bitReverse_i24(1, -8388608)) return 2;
	if (!bitReverse_i24(-1, -1)) return 2;
	if (!bitReverse_i24(-8388608, 1)) return 2;
	if (!bitReverse_i24(-5517841, -543787)) return 2;
	if (!byteSwap_i24(0, 0)) return 2;
	if (!byteSwap_i24(1, 65536)) return 2;
	if (!byteSwap_i24(-1, -1)) return 2;
	if (!byteSwap_i24(-8388608, 128)) return 2;
	if (!byteSwap_i24(-5517841, -1061461)) return 2;
	if (!bitReverse_u25(0, 0)) return 2;
	if (!bitReverse_u25(1, 16777216)) return 2;
	if (!bitReverse_u25(33554431, 33554431)) return 2;
	if (!bitReverse_u25(16777216, 1)) return 2;
	if (!bitReverse_u25(28036591, 32466859)) return 2;
	if (!bitReverse_i25(0, 0)) return 2;
	if (!bitReverse_i25(1, -16777216)) return 2;
	if (!bitReverse_i25(-1, -1)) return 2;
	if (!bitReverse_i25(-16777216, 1)) return 2;
	if (!bitReverse_i25(-5517841, -1087573)) return 2;
	if (!bitReverse_u26(0, 0)) return 2;
	if (!bitReverse_u26(1, 33554432)) return 2;
	if (!bitReverse_u26(67108863, 67108863)) return 2;
	if (!bitReverse_u26(33554432, 1)) return 2;
	if (!bitReverse_u26(28036591, 64933718)) return 2;
	if (!bitReverse_i26(0, 0)) return 2;
	if (!bitReverse_i26(1, -33554432)) return 2;
	if (!bitReverse_i26(-1, -1)) return 2;
	if (!bitReverse_i26(-33554432, 1)) return 2;
	if (!bitReverse_i26(28036591, -2175146)) return 2;
	if (!bitReverse_u27(0, 0)) return 2;
	if (!bitReverse_u27(1, 67108864)) return 2;
	if (!bitReverse_u27(134217727, 134217727)) return 2;
	if (!bitReverse_u27(67108864, 1)) return 2;
	if (!bitReverse_u27(28036591, 129867436)) return 2;
	if (!bitReverse_i27(0, 0)) return 2;
	if (!bitReverse_i27(1, -67108864)) return 2;
	if (!bitReverse_i27(-1, -1)) return 2;
	if (!bitReverse_i27(-67108864, 1)) return 2;
	if (!bitReverse_i27(28036591, -4350292)) return 2;
	if (!bitReverse_u28(0, 0)) return 2;
	if (!bitReverse_u28(1, 134217728)) return 2;
	if (!bitReverse_u28(268435455, 268435455)) return 2;
	if (!bitReverse_u28(134217728, 1)) return 2;
	if (!bitReverse_u28(162254319, 259734873)) return 2;
	if (!bitReverse_i28(0, 0)) return 2;
	if (!bitReverse_i28(1, -134217728)) return 2;
	if (!bitReverse_i28(-1, -1)) return 2;
	if (!bitReverse_i28(-134217728, 1)) return 2;
	if (!bitReverse_i28(-106181137, -8700583)) return 2;
	if (!bitReverse_u29(0, 0)) return 2;
	if (!bitReverse_u29(1, 268435456)) return 2;
	if (!bitReverse_u29(536870911, 536870911)) return 2;
	if (!bitReverse_u29(268435456, 1)) return 2;
	if (!bitReverse_u29(162254319, 519469746)) return 2;
	if (!bitReverse_i29(0, 0)) return 2;
	if (!bitReverse_i29(1, -268435456)) return 2;
	if (!bitReverse_i29(-1, -1)) return 2;
	if (!bitReverse_i29(-268435456, 1)) return 2;
	if (!bitReverse_i29(162254319, -17401166)) return 2;
	if (!bitReverse_u30(0, 0)) return 2;
	if (!bitReverse_u30(1, 536870912)) return 2;
	if (!bitReverse_u30(1073741823, 1073741823)) return 2;
	if (!bitReverse_u30(536870912, 1)) return 2;
	if (!bitReverse_u30(162254319, 1038939492)) return 2;
	if (!bitReverse_i30(0, 0)) return 2;
	if (!bitReverse_i30(1, -536870912)) return 2;
	if (!bitReverse_i30(-1, -1)) return 2;
	if (!bitReverse_i30(-536870912, 1)) return 2;
	if (!bitReverse_i30(162254319, -34802332)) return 2;
	if (!bitReverse_u31(0, 0)) return 2;
	if (!bitReverse_u31(1, 1073741824)) return 2;
	if (!bitReverse_u31(2147483647, 2147483647)) return 2;
	if (!bitReverse_u31(1073741824, 1)) return 2;
	if (!bitReverse_u31(162254319, 2077878984)) return 2;
	if (!bitReverse_i31(0, 0)) return 2;
	if (!bitReverse_i31(1, -1073741824)) return 2;
	if (!bitReverse_i31(-1, -1)) return 2;
	if (!bitReverse_i31(-1073741824, 1)) return 2;
	if (!bitReverse_i31(162254319, -69604664)) return 2;
	if (!bitReverse_u32(0, 0)) return 2;
	if (!bitReverse_u32(1, 2147483648)) return 2;
	if (!bitReverse_u32(4294967295, 4294967295)) return 2;
	if (!bitReverse_u32(2147483648, 1)) return 2;
	if (!bitReverse_u32(2309737967, 4155757969)) return 2;
	if (!byteSwap_u32(0, 0)) return 2;
	if (!byteSwap_u32(1, 16777216)) return 2;
	if (!byteSwap_u32(4294967295, 4294967295)) return 2;
	if (!byteSwap_u32(2147483648, 128)) return 2;
	if (!byteSwap_u32(2309737967, 4023233417)) return 2;
	if (!bitReverse_i32(0, 0)) return 2;
	if (!bitReverse_i32(1, -2147483648)) return 2;
	if (!bitReverse_i32(-1, -1)) return 2;
	if (!bitReverse_i32(-2147483648, 1)) return 2;
	if (!bitReverse_i32(-1985229329, -139209327)) return 2;
	if (!byteSwap_i32(0, 0)) return 2;
	if (!byteSwap_i32(1, 16777216)) return 2;
	if (!byteSwap_i32(-1, -1)) return 2;
	if (!byteSwap_i32(-2147483648, 128)) return 2;
	if (!byteSwap_i32(-1985229329, -271733879)) return 2;
	if (!bitReverse_u33(0, 0)) return 2;
	if (!bitReverse_u33(1, 4294967296)) return 2;
	if (!bitReverse_u33(8589934591, 8589934591)) return 2;
	if (!bitReverse_u33(4294967296, 1)) return 2;
	if (!bitReverse_u33(6604705263, 8311515939)) return 2;
	if (!bitReverse_i33(0, 0)) return 2;
	if (!bitReverse_i33(1, -4294967296)) return 2;
	if (!bitReverse_i33(-1, -1)) return 2;
	if (!bitReverse_i33(-4294967296, 1)) return 2;
	if (!bitReverse_i33(-1985229329, -278418653)) return 2;
	if (!bitReverse_u34(0, 0)) return 2;
	if (!bitReverse_u34(1, 8589934592)) return 2;
	if (!bitReverse_u34(17179869183, 17179869183)) return 2;
	if (!bitReverse_u34(8589934592, 1)) return 2;
	if (!bitReverse_u34(15194639855, 16623031879)) return 2;
	if (!bitReverse_i34(0, 0)) return 2;
	if (!bitReverse_i34(1, -8589934592)) return 2;
	if (!bitReverse_i34(-1, -1)) return 2;
	if (!bitReverse_i34(-8589934592, 1)) return 2;
	if (!bitReverse_i34(-1985229329, -556837305)) return 2;
	if (!bitReverse_u35(0, 0)) return 2;
	if (!bitReverse_u35(1, 17179869184)) return 2;
	if (!bitReverse_u35(34359738367, 34359738367)) return 2;
	if (!bitReverse_u35(17179869184, 1)) return 2;
	if (!bitReverse_u35(32374509039, 33246063759)) return 2;
	if (!bitReverse_i35(0, 0)) return 2;
	if (!bitReverse_i35(1, -17179869184)) return 2;
	if (!bitReverse_i35(-1, -1)) return 2;
	if (!bitReverse_i35(-17179869184, 1)) return 2;
	if (!bitReverse_i35(-1985229329, -1113674609)) return 2;
	if (!bitReverse_u36(0, 0)) return 2;
	if (!bitReverse_u36(1, 34359738368)) return 2;
	if (!bitReverse_u36(68719476735, 68719476735)) return 2;
	if (!bitReverse_u36(34359738368, 1)) return 2;
	if (!bitReverse_u36(32374509039, 66492127518)) return 2;
	if (!bitReverse_i36(0, 0)) return 2;
	if (!bitReverse_i36(1, -34359738368)) return 2;
	if (!bitReverse_i36(-1, -1)) return 2;
	if (!bitReverse_i36(-34359738368, 1)) return 2;
	if (!bitReverse_i36(32374509039, -2227349218)) return 2;
	if (!bitReverse_u37(0, 0)) return 2;
	if (!bitReverse_u37(1, 68719476736)) return 2;
	if (!bitReverse_u37(137438953471, 137438953471)) return 2;
	if (!bitReverse_u37(68719476736, 1)) return 2;
	if (!bitReverse_u37(32374509039, 132984255036)) return 2;
	if (!bitReverse_i37(0, 0)) return 2;
	if (!bitReverse_i37(1, -68719476736)) return 2;
	if (!bitReverse_i37(-1, -1)) return 2;
	if (!bitReverse_i37(-68719476736, 1)) return 2;
	if (!bitReverse_i37(32374509039, -4454698436)) return 2;
	if (!bitReverse_u38(0, 0)) return 2;
	if (!bitReverse_u38(1, 137438953472)) return 2;
	if (!bitReverse_u38(274877906943, 274877906943)) return 2;
	if (!bitReverse_u38(137438953472, 1)) return 2;
	if (!bitReverse_u38(169813462511, 265968510073)) return 2;
	if (!bitReverse_i38(0, 0)) return 2;
	if (!bitReverse_i38(1, -137438953472)) return 2;
	if (!bitReverse_i38(-1, -1)) return 2;
	if (!bitReverse_i38(-137438953472, 1)) return 2;
	if (!bitReverse_i38(-105064444433, -8909396871)) return 2;
	if (!bitReverse_u39(0, 0)) return 2;
	if (!bitReverse_u39(1, 274877906944)) return 2;
	if (!bitReverse_u39(549755813887, 549755813887)) return 2;
	if (!bitReverse_u39(274877906944, 1)) return 2;
	if (!bitReverse_u39(444691369455, 531937020147)) return 2;
	if (!bitReverse_i39(0, 0)) return 2;
	if (!bitReverse_i39(1, -274877906944)) return 2;
	if (!bitReverse_i39(-1, -1)) return 2;
	if (!bitReverse_i39(-274877906944, 1)) return 2;
	if (!bitReverse_i39(-105064444433, -17818793741)) return 2;
	if (!bitReverse_u40(0, 0)) return 2;
	if (!bitReverse_u40(1, 549755813888)) return 2;
	if (!bitReverse_u40(1099511627775, 1099511627775)) return 2;
	if (!bitReverse_u40(549755813888, 1)) return 2;
	if (!bitReverse_u40(444691369455, 1063874040294)) return 2;
	if (!byteSwap_u40(0, 0)) return 2;
	if (!byteSwap_u40(1, 4294967296)) return 2;
	if (!byteSwap_u40(1099511627775, 1099511627775)) return 2;
	if (!byteSwap_u40(549755813888, 128)) return 2;
	if (!byteSwap_u40(444691369455, 1029947754855)) return 2;
	if (!bitReverse_i40(0, 0)) return 2;
	if (!bitReverse_i40(1, -549755813888)) return 2;
	if (!bitReverse_i40(-1, -1)) return 2;
	if (!bitReverse_i40(-549755813888, 1)) return 2;
	if (!bitReverse_i40(444691369455, -35637587482)) return 2;
	if (!byteSwap_i40(0, 0)) return 2;
	if (!byteSwap_i40(1, 4294967296)) return 2;
	if (!byteSwap_i40(-1, -1)) return 2;
	if (!byteSwap_i40(-549755813888, 128)) return 2;
	if (!byteSwap_i40(444691369455, -69563872921)) return 2;
	if (!bitReverse_u41(0, 0)) return 2;
	if (!bitReverse_u41(1, 1099511627776)) return 2;
	if (!bitReverse_u41(2199023255551, 2199023255551)) return 2;
	if (!bitReverse_u41(1099511627776, 1)) return 2;
	if (!bitReverse_u41(1544202997231, 2127748080589)) return 2;
	if (!bitReverse_i41(0, 0)) return 2;
	if (!bitReverse_i41(1, -1099511627776)) return 2;
	if (!bitReverse_i41(-1, -1)) return 2;
	if (!bitReverse_i41(-1099511627776, 1)) return 2;
	if (!bitReverse_i41(-654820258321, -71275174963)) return 2;
	if (!bitReverse_u42(0, 0)) return 2;
	if (!bitReverse_u42(1, 2199023255552)) return 2;
	if (!bitReverse_u42(4398046511103, 4398046511103)) return 2;
	if (!bitReverse_u42(2199023255552, 1)) return 2;
	if (!bitReverse_u42(1544202997231, 4255496161178)) return 2;
	if (!bitReverse_i42(0, 0)) return 2;
	if (!bitReverse_i42(1, -2199023255552)) return 2;
	if (!bitReverse_i42(-1, -1)) return 2;
	if (!bitReverse_i42(-2199023255552, 1)) return 2;
	if (!bitReverse_i42(1544202997231, -142550349926)) return 2;
	if (!bitReverse_u43(0, 0)) return 2;
	if (!bitReverse_u43(1, 4398046511104)) return 2;
	if (!bitReverse_u43(8796093022207, 8796093022207)) return 2;
	if (!bitReverse_u43(4398046511104, 1)) return 2;
	if (!bitReverse_u43(5942249508335, 8510992322357)) return 2;
	if (!bitReverse_i43(0, 0)) return 2;
	if (!bitReverse_i43(1, -4398046511104)) return 2;
	if (!bitReverse_i43(-1, -1)) return 2;
	if (!bitReverse_i43(-4398046511104, 1)) return 2;
	if (!bitReverse_i43(-2853843513873, -285100699851)) return 2;
	if (!bitReverse_u44(0, 0)) return 2;
	if (!bitReverse_u44(1, 8796093022208)) return 2;
	if (!bitReverse_u44(17592186044415, 17592186044415)) return 2;
	if (!bitReverse_u44(8796093022208, 1)) return 2;
	if (!bitReverse_u44(5942249508335, 17021984644714)) return 2;
	if (!bitReverse_i44(0, 0)) return 2;
	if (!bitReverse_i44(1, -8796093022208)) return 2;
	if (!bitReverse_i44(-1, -1)) return 2;
	if (!bitReverse_i44(-8796093022208, 1)) return 2;
	if (!bitReverse_i44(5942249508335, -570201399702)) return 2;
	if (!bitReverse_u45(0, 0)) return 2;
	if (!bitReverse_u45(1, 17592186044416)) return 2;
	if (!bitReverse_u45(35184372088831, 35184372088831)) return 2;
	if (!bitReverse_u45(17592186044416, 1)) return 2;
	if (!bitReverse_u45(5942249508335, 34043969289428)) return 2;
	if (!bitReverse_i45(0, 0)) return 2;
	if (!bitReverse_i45(1, -17592186044416)) return 2;
	if (!bitReverse_i45(-1, -1)) return 2;
	if (!bitReverse_i45(-17592186044416, 1)) return 2;
	if (!bitReverse_i45(5942249508335, -1140402799404)) return 2;
	if (!bitReverse_u46(0, 0)) return 2;
	if (!bitReverse_u46(1, 35184372088832)) return 2;
	if (!bitReverse_u46(70368744177663, 70368744177663)) return 2;
	if (!bitReverse_u46(35184372088832, 1)) return 2;
	if (!bitReverse_u46(5942249508335, 68087938578856)) return 2;
	if (!bitReverse_i46(0, 0)) return 2;
	if (!bitReverse_i46(1, -35184372088832)) return 2;
	if (!bitReverse_i46(-1, -1)) return 2;
	if (!bitReverse_i46(-35184372088832, 1)) return 2;
	if (!bitReverse_i46(5942249508335, -2280805598808)) return 2;
	if (!bitReverse_u47(0, 0)) return 2;
	if (!bitReverse_u47(1, 70368744177664)) return 2;
	if (!bitReverse_u47(140737488355327, 140737488355327)) return 2;
	if (!bitReverse_u47(70368744177664, 1)) return 2;
	if (!bitReverse_u47(76310993685999, 136175877157713)) return 2;
	if (!bitReverse_i47(0, 0)) return 2;
	if (!bitReverse_i47(1, -70368744177664)) return 2;
	if (!bitReverse_i47(-1, -1)) return 2;
	if (!bitReverse_i47(-70368744177664, 1)) return 2;
	if (!bitReverse_i47(-64426494669329, -4561611197615)) return 2;
	if (!bitReverse_u48(0, 0)) return 2;
	if (!bitReverse_u48(1, 140737488355328)) return 2;
	if (!bitReverse_u48(281474976710655, 281474976710655)) return 2;
	if (!bitReverse_u48(140737488355328, 1)) return 2;
	if (!bitReverse_u48(76310993685999, 272351754315426)) return 2;
	if (!byteSwap_u48(0, 0)) return 2;
	if (!byteSwap_u48(1, 1099511627776)) return 2;
	if (!byteSwap_u48(281474976710655, 281474976710655)) return 2;
	if (!byteSwap_u48(140737488355328, 128)) return 2;
	if (!byteSwap_u48(76310993685999, 263666625242949)) return 2;
	if (!bitReverse_i48(0, 0)) return 2;
	if (!bitReverse_i48(1, -140737488355328)) return 2;
	if (!bitReverse_i48(-1, -1)) return 2;
	if (!bitReverse_i48(-140737488355328, 1)) return 2;
	if (!bitReverse_i48(76310993685999, -9123222395230)) return 2;
	if (!byteSwap_i48(0, 0)) return 2;
	if (!byteSwap_i48(1, 1099511627776)) return 2;
	if (!byteSwap_i48(-1, -1)) return 2;
	if (!byteSwap_i48(-140737488355328, 128)) return 2;
	if (!byteSwap_i48(76310993685999, -17808351467707)) return 2;
	if (!bitReverse_u49(0, 0)) return 2;
	if (!bitReverse_u49(1, 281474976710656)) return 2;
	if (!bitReverse_u49(562949953421311, 562949953421311)) return 2;
	if (!bitReverse_u49(281474976710656, 1)) return 2;
	if (!bitReverse_u49(357785970396655, 544703508630853)) return 2;
	if (!bitReverse_i49(0, 0)) return 2;
	if (!bitReverse_i49(1, -281474976710656)) return 2;
	if (!bitReverse_i49(-1, -1)) return 2;
	if (!bitReverse_i49(-281474976710656, 1)) return 2;
	if (!bitReverse_i49(-205163983024657, -18246444790459)) return 2;
	if (!bitReverse_u50(0, 0)) return 2;
	if (!bitReverse_u50(1, 562949953421312)) return 2;
	if (!bitReverse_u50(1125899906842623, 1125899906842623)) return 2;
	if (!bitReverse_u50(562949953421312, 1)) return 2;
	if (!bitReverse_u50(920735923817967, 1089407017261707)) return 2;
	if (!bitReverse_i50(0, 0)) return 2;
	if (!bitReverse_i50(1, -562949953421312)) return 2;
	if (!bitReverse_i50(-1, -1)) return 2;
	if (!bitReverse_i50(-562949953421312, 1)) return 2;
	if (!bitReverse_i50(-205163983024657, -36492889580917)) return 2;
	if (!bitReverse_u51(0, 0)) return 2;
	if (!bitReverse_u51(1, 1125899906842624)) return 2;
	if (!bitReverse_u51(2251799813685247, 2251799813685247)) return 2;
	if (!bitReverse_u51(1125899906842624, 1)) return 2;
	if (!bitReverse_u51(920735923817967, 2178814034523414)) return 2;
	if (!bitReverse_i51(0, 0)) return 2;
	if (!bitReverse_i51(1, -1125899906842624)) return 2;
	if (!bitReverse_i51(-1, -1)) return 2;
	if (!bitReverse_i51(-1125899906842624, 1)) return 2;
	if (!bitReverse_i51(920735923817967, -72985779161834)) return 2;
	if (!bitReverse_u52(0, 0)) return 2;
	if (!bitReverse_u52(1, 2251799813685248)) return 2;
	if (!bitReverse_u52(4503599627370495, 4503599627370495)) return 2;
	if (!bitReverse_u52(2251799813685248, 1)) return 2;
	if (!bitReverse_u52(920735923817967, 4357628069046828)) return 2;
	if (!bitReverse_i52(0, 0)) return 2;
	if (!bitReverse_i52(1, -2251799813685248)) return 2;
	if (!bitReverse_i52(-1, -1)) return 2;
	if (!bitReverse_i52(-2251799813685248, 1)) return 2;
	if (!bitReverse_i52(920735923817967, -145971558323668)) return 2;
	if (!bitReverse_u53(0, 0)) return 2;
	if (!bitReverse_u53(1, 4503599627370496)) return 2;
	if (!bitReverse_u53(9007199254740991, 9007199254740991)) return 2;
	if (!bitReverse_u53(4503599627370496, 1)) return 2;
	if (!bitReverse_u53(920735923817967, 8715256138093656)) return 2;
	if (!bitReverse_i53(0, 0)) return 2;
	if (!bitReverse_i53(1, -4503599627370496)) return 2;
	if (!bitReverse_i53(-1, -1)) return 2;
	if (!bitReverse_i53(-4503599627370496, 1)) return 2;
	if (!bitReverse_i53(920735923817967, -291943116647336)) return 2;
	if (!bitReverse_u54(0, 0)) return 2;
	if (!bitReverse_u54(1, 9007199254740992)) return 2;
	if (!bitReverse_u54(18014398509481983, 18014398509481983)) return 2;
	if (!bitReverse_u54(9007199254740992, 1)) return 2;
	if (!bitReverse_u54(9927935178558959, 17430512276187313)) return 2;
	if (!bitReverse_i54(0, 0)) return 2;
	if (!bitReverse_i54(1, -9007199254740992)) return 2;
	if (!bitReverse_i54(-1, -1)) return 2;
	if (!bitReverse_i54(-9007199254740992, 1)) return 2;
	if (!bitReverse_i54(-8086463330923025, -583886233294671)) return 2;
	if (!bitReverse_u55(0, 0)) return 2;
	if (!bitReverse_u55(1, 18014398509481984)) return 2;
	if (!bitReverse_u55(36028797018963967, 36028797018963967)) return 2;
	if (!bitReverse_u55(18014398509481984, 1)) return 2;
	if (!bitReverse_u55(9927935178558959, 34861024552374626)) return 2;
	if (!bitReverse_i55(0, 0)) return 2;
	if (!bitReverse_i55(1, -18014398509481984)) return 2;
	if (!bitReverse_i55(-1, -1)) return 2;
	if (!bitReverse_i55(-18014398509481984, 1)) return 2;
	if (!bitReverse_i55(9927935178558959, -1167772466589342)) return 2;
	if (!bitReverse_u56(0, 0)) return 2;
	if (!bitReverse_u56(1, 36028797018963968)) return 2;
	if (!bitReverse_u56(72057594037927935, 72057594037927935)) return 2;
	if (!bitReverse_u56(36028797018963968, 1)) return 2;
	if (!bitReverse_u56(9927935178558959, 69722049104749252)) return 2;
	if (!byteSwap_u56(0, 0)) return 2;
	if (!byteSwap_u56(1, 281474976710656)) return 2;
	if (!byteSwap_u56(72057594037927935, 72057594037927935)) return 2;
	if (!byteSwap_u56(36028797018963968, 128)) return 2;
	if (!byteSwap_u56(9927935178558959, 67498656062194979)) return 2;
	if (!bitReverse_i56(0, 0)) return 2;
	if (!bitReverse_i56(1, -36028797018963968)) return 2;
	if (!bitReverse_i56(-1, -1)) return 2;
	if (!bitReverse_i56(-36028797018963968, 1)) return 2;
	if (!bitReverse_i56(9927935178558959, -2335544933178684)) return 2;
	if (!byteSwap_i56(0, 0)) return 2;
	if (!byteSwap_i56(1, 281474976710656)) return 2;
	if (!byteSwap_i56(-1, -1)) return 2;
	if (!byteSwap_i56(-36028797018963968, 128)) return 2;
	if (!byteSwap_i56(9927935178558959, -4558937975732957)) return 2;
	if (!bitReverse_u57(0, 0)) return 2;
	if (!bitReverse_u57(1, 72057594037927936)) return 2;
	if (!bitReverse_u57(144115188075855871, 144115188075855871)) return 2;
	if (!bitReverse_u57(72057594037927936, 1)) return 2;
	if (!bitReverse_u57(81985529216486895, 139444098209498505)) return 2;
	if (!bitReverse_i57(0, 0)) return 2;
	if (!bitReverse_i57(1, -72057594037927936)) return 2;
	if (!bitReverse_i57(-1, -1)) return 2;
	if (!bitReverse_i57(-72057594037927936, 1)) return 2;
	if (!bitReverse_i57(-62129658859368977, -4671089866357367)) return 2;
	if (!bitReverse_u58(0, 0)) return 2;
	if (!bitReverse_u58(1, 144115188075855872)) return 2;
	if (!bitReverse_u58(288230376151711743, 288230376151711743)) return 2;
	if (!bitReverse_u58(144115188075855872, 1)) return 2;
	if (!bitReverse_u58(81985529216486895, 278888196418997010)) return 2;
	if (!bitReverse_i58(0, 0)) return 2;
	if (!bitReverse_i58(1, -144115188075855872)) return 2;
	if (!bitReverse_i58(-1, -1)) return 2;
	if (!bitReverse_i58(-144115188075855872, 1)) return 2;
	if (!bitReverse_i58(81985529216486895, -9342179732714734)) return 2;
	if (!bitReverse_u59(0, 0)) return 2;
	if (!bitReverse_u59(1, 288230376151711744)) return 2;
	if (!bitReverse_u59(576460752303423487, 576460752303423487)) return 2;
	if (!bitReverse_u59(288230376151711744, 1)) return 2;
	if (!bitReverse_u59(81985529216486895, 557776392837994020)) return 2;
	if (!bitReverse_i59(0, 0)) return 2;
	if (!bitReverse_i59(1, -288230376151711744)) return 2;
	if (!bitReverse_i59(-1, -1)) return 2;
	if (!bitReverse_i59(-288230376151711744, 1)) return 2;
	if (!bitReverse_i59(81985529216486895, -18684359465429468)) return 2;
	if (!bitReverse_u60(0, 0)) return 2;
	if (!bitReverse_u60(1, 576460752303423488)) return 2;
	if (!bitReverse_u60(1152921504606846975, 1152921504606846975)) return 2;
	if (!bitReverse_u60(576460752303423488, 1)) return 2;
	if (!bitReverse_u60(81985529216486895, 1115552785675988040)) return 2;
	if (!bitReverse_i60(0, 0)) return 2;
	if (!bitReverse_i60(1, -576460752303423488)) return 2;
	if (!bitReverse_i60(-1, -1)) return 2;
	if (!bitReverse_i60(-576460752303423488, 1)) return 2;
	if (!bitReverse_i60(81985529216486895, -37368718930858936)) return 2;
	if (!bitReverse_u61(0, 0)) return 2;
	if (!bitReverse_u61(1, 1152921504606846976)) return 2;
	if (!bitReverse_u61(2305843009213693951, 2305843009213693951)) return 2;
	if (!bitReverse_u61(1152921504606846976, 1)) return 2;
	if (!bitReverse_u61(81985529216486895, 2231105571351976080)) return 2;
	if (!bitReverse_i61(0, 0)) return 2;
	if (!bitReverse_i61(1, -1152921504606846976)) return 2;
	if (!bitReverse_i61(-1, -1)) return 2;
	if (!bitReverse_i61(-1152921504606846976, 1)) return 2;
	if (!bitReverse_i61(81985529216486895, -74737437861717872)) return 2;
	if (!bitReverse_u62(0, 0)) return 2;
	if (!bitReverse_u62(1, 2305843009213693952)) return 2;
	if (!bitReverse_u62(4611686018427387903, 4611686018427387903)) return 2;
	if (!bitReverse_u62(2305843009213693952, 1)) return 2;
	if (!bitReverse_u62(81985529216486895, 4462211142703952160)) return 2;
	if (!bitReverse_i62(0, 0)) return 2;
	if (!bitReverse_i62(1, -2305843009213693952)) return 2;
	if (!bitReverse_i62(-1, -1)) return 2;
	if (!bitReverse_i62(-2305843009213693952, 1)) return 2;
	if (!bitReverse_i62(81985529216486895, -149474875723435744)) return 2;
	if (!bitReverse_u63(0, 0)) return 2;
	if (!bitReverse_u63(1, 4611686018427387904)) return 2;
	if (!bitReverse_u63(9223372036854775807, 9223372036854775807)) return 2;
	if (!bitReverse_u63(4611686018427387904, 1)) return 2;
	if (!bitReverse_u63(81985529216486895, 8924422285407904320)) return 2;
	if (!bitReverse_i63(0, 0)) return 2;
	if (!bitReverse_i63(1, -4611686018427387904)) return 2;
	if (!bitReverse_i63(-1, -1)) return 2;
	if (!bitReverse_i63(-4611686018427387904, 1)) return 2;
	if (!bitReverse_i63(81985529216486895, -298949751446871488)) return 2;
	if (!bitReverse_u64(0, 0)) return 2;
	if (!bitReverse_u64(1, 9223372036854775808)) return 2;
	if (!bitReverse_u64(18446744073709551615, 18446744073709551615)) return 2;
	if (!bitReverse_u64(9223372036854775808, 1)) return 2;
	if (!bitReverse_u64(81985529216486895, 17848844570815808640)) return 2;
	if (!byteSwap_u64(0, 0)) return 2;
	if (!byteSwap_u64(1, 72057594037927936)) return 2;
	if (!byteSwap_u64(18446744073709551615, 18446744073709551615)) return 2;
	if (!byteSwap_u64(9223372036854775808, 128)) return 2;
	if (!byteSwap_u64(81985529216486895, 17279655951921914625)) return 2;
	if (!bitReverse_i64(0, 0)) return 2;
	if (!bitReverse_i64(1, -9223372036854775808)) return 2;
	if (!bitReverse_i64(-1, -1)) return 2;
	if (!bitReverse_i64(-9223372036854775808, 1)) return 2;
	if (!bitReverse_i64(81985529216486895, -597899502893742976)) return 2;
	if (!byteSwap_i64(0, 0)) return 2;
	if (!byteSwap_i64(1, 72057594037927936)) return 2;
	if (!byteSwap_i64(-1, -1)) return 2;
	if (!byteSwap_i64(-9223372036854775808, 128)) return 2;
	if (!byteSwap_i64(81985529216486895, -1167088121787636991)) return 2;
	var calls: u8 = 0;
	if (@bitReverse(advance(&calls)) != 1 or @byteSwap(advance(&calls)) != 128 or calls != 2) return 3;
	if (@bitReverse(zero(&calls)) != 0 or @byteSwap(zero(&calls)) != 0 or calls != 4) return 4;
	return 0;
}
