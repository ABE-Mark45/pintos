gdb kernel.o -tui -ex 'target remote 0.0.0.0:1234' -ex 'b timer_interrupt' -ex 'list'
