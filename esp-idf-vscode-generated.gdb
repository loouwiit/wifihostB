target remote :3333
symbol-file ./build/main.elf
mon reset halt
flushregs
thb app_main