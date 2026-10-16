// exit: 0
fn reverse(n:i5) i5 { return @bitReverse(n); }
fn swap(n:i24) i24 { return @byteSwap(n); }
pub fn main() u8 {
	if (reverse(1) != -16 or swap(128) != -8388608) return 1;
	const reverse_wide = @bitReverse(@as(u257,1));
	const check_reverse:u8=if(reverse_wide == 115792089237316195423570985008687907853269984665640564039457584007913129639936)0 else 256;
	_ = check_reverse;
	const swap_wide = @byteSwap(@as(i128,1));
	const check_swap:u8=if(swap_wide == 1329227995784915872903807060280344576)0 else 256;
	_ = check_swap;
	return 0;
}
