const S=struct { self:S, };
pub fn main() void { _=@offsetOf(S,3); }
