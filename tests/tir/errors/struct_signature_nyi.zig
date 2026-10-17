const S=struct { x:u8, };
fn consume(value:S) void { _=value; }
pub fn main() void { consume(undefined); }
