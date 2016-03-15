ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS = -I$(top_srcdir)/src 
AM_MAKEFLAGS = -s

include_HEADERS=multi-view/mv_ioctl.h

# Put the list of kernel modules to be built here.
# Then, modify install, uninstall, and clean hooks to 
# account for the generated modules.
# Finally, there should be one rule for each kernel module.
# Each module's subfolder must have its own Makefile
# (force added to the repository) which is used then by the
# kernel to build the module. CFLAGS must be explicitly reset
# to remove all the sh*t placed by autotools, which the kernel
# doesnt like. To pass parameters to the compilation of the module
# EXTRA_CFLAGS should be used.
all-local: multi-view.ko intercept.ko


# Per-module rules
multi-view.ko: multi-view/multiview.c multi-view/tracking_accesses.c
	$(MAKE) -C $(KERNEL_SRC) M=$(abs_srcdir)/multi-view/ CFLAGS="" EXTRA_CFLAGS="-DSCT_ADDR=$(SCT_ADDR)\
	       	-DSNS_ADDR=$(SNS_ADDR)\
	       	-DSMU_ADDR=$(SMU_ADDR)"

intercept.ko: intercept/intercept_page_fault.c intercept/my_page_fault.c
	cp $(abs_srcdir)/multi-view/Module.symvers intercept/
	$(MAKE) -C $(KERNEL_SRC) M=$(abs_srcdir)/intercept/ CFLAGS=""


# Autotools hooks to install, uninstall and clean
install-exec-hook: all-local
	$(INSTALL) $(abs_srcdir)/multi-view/multi-view.ko $(DESTDIR)$(KERNEL_MOD)/multi-view.ko
	$(INSTALL) $(abs_srcdir)/intercept/intercept.ko $(DESTDIR)$(KERNEL_MOD)/intercept.ko
	depmod -a

uninstall-hook:
	$(RM) $(DESTDIR)$(KERNEL_MOD)/multi-view.ko
	$(RM) $(DESTDIR)$(KERNEL_MOD)/intercept.ko
	depmod -a

clean-local:
	-$(MAKE) -C $(KERNEL_SRC) M=$(abs_srcdir)/multi-view/ clean
	-$(MAKE) -C $(KERNEL_SRC) M=$(abs_srcdir)/intercept/ clean

load:
	./load.sh

unload:
	./unload.sh

run:
	cat /sys/module/multi_view_integrated/parameters/the_hook > /sys/module/schedule_hook/parameters/the_hook

shutdown:
	echo '0' > /sys/module/schedule_hook/parameters/the_hook

