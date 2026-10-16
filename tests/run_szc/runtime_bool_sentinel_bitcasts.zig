// exit: 0
// Zig 0.17.0 Air/Legalize.zig scalarizeBitcastBlockPayload uses arrayLen
// instead of arrayLenIncludingSentinel in both generic packing loops.
// Both official x86_64 backends pack .{false,true} as 2, not 6; the
// constant evaluator includes the true sentinel. Unpacking leaves the
// sentinel uninitialized. Keep the correct logical representation scoped,
// rather than reproducing that upstream runtime lowering defect.
fn pack(a:[2:true]bool) u3 { return @bitCast(a); }
fn unpack(a:u3) [2:true]bool { return @bitCast(a); }
pub fn main() u8 {
 if(pack(.{false,true})!=6) return 1;
 const a=unpack(6);
 if(a[0] or !a[1] or !a[2]) return 2;
 return 0;
}
