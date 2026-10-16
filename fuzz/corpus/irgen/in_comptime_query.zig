const T=@TypeOf(blk: { const n:u8=if(!@inComptime())0 else 256; _=n; break :blk false; }); pub fn main() void { _=T; }
