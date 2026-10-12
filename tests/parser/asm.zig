fn syscall0(number: usize) usize {
    return asm volatile ("syscall"
        : [ret] "={rax}" (-> usize),
        : [number] "{rax}" (number),
        : .{ .rcx = true, .r11 = true });
}
fn nop() void {
    asm volatile ("nop");
}
