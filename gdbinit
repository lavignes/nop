set disassembly-flavor intel
target remote localhost:1234
file bin/boot.elf
break _SYSQUIT

tui enable
tui layout regs
tui focus cmd

