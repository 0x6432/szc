const S=struct { child:[0]S, };
pub fn main() void { _=@hasField(S,"absent"); }
