pintos-gdb kernel.o -tui -ex 'set arch i386:x86-64' -ex 'target remote 0.0.0.0:1234' -ex 'b test_alarm_negative' -ex 'list'
