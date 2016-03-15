all:
	g++ -g --std=c++0x test-multiview.c migrate.c -lnuma -DSYSCALL_NUM=$(shell cat /sys/module/multi_view/parameters/syscall_number) -o migrate
