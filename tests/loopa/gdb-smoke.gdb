# GLOS M1: gdb attached to the kernel's stub on COM2 (tools/gdb-loopa.sh
# connects first). Stopped at the /GDB breakpoint in kmain.
set confirm off
set pagination off
info registers eip
break timer_start
continue
info registers eip
backtrace 3
stepi
info registers eip
print/x cpu_cr4_bits
delete
detach
