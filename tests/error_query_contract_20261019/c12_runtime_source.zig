const E=error{A,B};
export fn load(p:*const E) bool { const x:error{A}=@errorCast(p.*); return x==error.A; }
export fn store(p:*E) void { p.*=@errorCast(error.A); }
pub fn main() void {}
