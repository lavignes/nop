set disassembly-flavor intel
target remote localhost:1234
file boot.elf
break _Panic

# print pascal string
# - $arg0 is pointer to pascal string
define ps
    set $ptr = (char *)$arg0
    set $len = *(unsigned char *)$ptr
    print *($ptr+1)@$len
end

# this is the format for a dict entry
# ALIGN 4
# .ascii "name"
# ALIGN 4
# .word codelen
# .byte flags
# .byte namelen
# .word linkto next entry

# print dict entry
define pd
    set $link = (void *)$arg0
    set $xt = $link+4
    set $namelen = *(unsigned char *)($link - 1)
    set $flags = *(unsigned char *)($link - 2)
    set $codelen = *(unsigned short *)($link - 4)
    set $name = (char *)((unsigned long)($link - 4 - $namelen) & 0xFFFFFFFC)
    print *$name@$namelen
    printf "link=$%08X xt=$%08X\n", $link, $xt
    printf "namelen=%d codelen=%d flags=$%02X\n", $namelen, $codelen, $flags
end

tui enable

tui new-layout nop {-horizontal src 1 regs 1} 2 status 0 cmd 1
tui layout nop
focus cmd

