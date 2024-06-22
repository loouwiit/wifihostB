target remote :3333
set remotetimeout 20
symbol-file ./build/main.elf
mon reset halt
maintenance flush register-cache
thb app_main