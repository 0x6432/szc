const Big=error{A,B}; const Small=error{A};
comptime { const x:Big=error.A; const y:Small=x; if(y==error.A) {} }
pub fn main() void {}
