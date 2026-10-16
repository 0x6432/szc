// exit: 0
fn to_int(a:[2]u5) u10 { return @bitCast(a); }
fn to_array(a:u10) [2]i5 { return @bitCast(a); }
fn to_bool(a:u3) [3]bool { return @bitCast(a); }
fn to_nested(a:u20) [2][2]u5 { return @bitCast(a); }
fn from_nested(a:[2][2]u5) u20 { return @bitCast(a); }
fn sent(a:u24) [2:17]u8 { return @bitCast(a); }
fn signed_pack(a:[2]i5) i10 { return @bitCast(a); }
fn booleans(a:[3]bool) u3 { return @bitCast(a); }
fn reshape(a:[2]u5) [5]u2 { return @bitCast(a); }
fn sentinel_pack(a:[2:17]u8) u24 { return @bitCast(a); }
fn zero(a:[2]u0) [0]u8 { return @bitCast(a); }
pub fn main() u8 {
 if (to_int(.{1,2})!=65) return 1;
 const a=to_array(63); if(a[0]!=-1 or a[1]!=1) return 2;
 const b=to_bool(5); if(!b[0] or b[1] or !b[2]) return 3;
 const c=to_nested(134209); if(c[0][0]!=1 or c[0][1]!=2 or c[1][0]!=3 or c[1][1]!=4) return 4;
 if(from_nested(c)!=134209) return 5;
 const d=sent(0x110201); if(d[0]!=1 or d[1]!=2 or d[2]!=17) return 6;
 if(signed_pack(.{-1,-1})!=-1 or booleans(.{true,false,true})!=5) return 7;
 const e=reshape(.{1,2}); if(e[0]!=1 or e[1]!=0 or e[2]!=0 or e[3]!=1 or e[4]!=0) return 8;
 if(sentinel_pack(.{1,2})!=0x110201) return 9;
 const z=zero(.{0,0}); if(z.len!=0) return 10;
 var original:[2]u8=.{1,2};
 const snapshot:u16=@bitCast(original); original[0]=9;
 if(snapshot!=513 or original[0]!=9) return 11;
 return 0;
}
