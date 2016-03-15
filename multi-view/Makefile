obj-m += multi-view.o
multi-view-objs = multiview.o tracking_accesses.o
all:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) modules
			
clean:
	make -C $(LINUX_KERNEL_PATH) M=$(CURRENT_PATH) clean

