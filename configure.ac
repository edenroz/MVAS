AC_PREREQ([2.63])
AC_INIT([multi-view], [1.0.0], [quaglia@dis.uniroma1.it,pellegrini@dis.uniroma1.it])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_MACRO_DIR([m4])

# Preliminary check: are we on Linux?
AC_CANONICAL_HOST
AC_MSG_CHECKING([for supported host Operating System])
case $host_os in
  linux*)
        AC_MSG_RESULT([yes, ${host_os}])
        ;;
    *)
        #Default Case
        AC_MSG_RESULT([no, ${host_os}])
        AC_MSG_ERROR([This module runs only on Linux])
        ;;
esac

# Preliminary check: are we on x86?
AC_MSG_CHECKING([for a supported CPU architecture])
case "${host_cpu}" in

     x86_64)
        AC_MSG_RESULT([yes, ${host_cpu}])
        ;;

     *)
       AC_MSG_RESULT([no, ${host_cpu}])
       AC_MSG_ERROR([Unsupported host architecture. Multi-view currently supports only x86_64 systems.])
       ;;
esac




# Configure kernel module paths
AC_SUBST([with_kernel], [`uname -r`])
AC_SUBST([with_kernel_mod], [/lib/modules/$with_kernel/extra])
AC_SUBST([KERNEL_SRC], [/lib/modules/$with_kernel/build])
AC_SUBST([KERNEL_MOD], [$with_kernel_mod])

# Checks for programs.
AC_LANG([C])
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_GREP
AC_PROG_SED
AC_PROG_RANLIB
AC_PATH_PROG(RM, rm, /bin/rm, $PATH:/bin:/usr/bin:/usr/local/bin)



# Get sys_call_table in the current kernel
AC_MSG_CHECKING([for sys_call_table in the current kernel])
sct_line=$($GREP -n " sys_call_table" /boot/System.map-$(uname -r) | $SED 's/:.*//')
sys_call_table=$($SED "${sct_line}q;d" /boot/System.map-$(uname -r) | $SED 's/ .*//')
if test -z "$sys_call_table"; then
        AC_MSG_ERROR([Address of sys_call_table not found in Kernel map])
fi
AC_MSG_RESULT([found at 0x$sys_call_table])
AC_SUBST([SCT_ADDR], [0x$sys_call_table])

# Get sys_ni_syscall in the current kernel
AC_MSG_CHECKING([for sys_ni_syscall in the current kernel])
sns_line=$($GREP -n " sys_ni_syscall" /boot/System.map-$(uname -r) | $SED 's/:.*//')
sys_ni_syscall=$($SED "${sns_line}q;d" /boot/System.map-$(uname -r) | $SED 's/ .*//')
if test -z "$sys_ni_syscall"; then
        AC_MSG_ERROR([Address of sys_ni_syscall not found in kernel map])
fi
AC_MSG_RESULT([found at 0x$sys_ni_syscall])
AC_SUBST([SNS_ADDR], [0x$sys_ni_syscall])

# Get sys_munmap in the current kernel
AC_MSG_CHECKING([for sys_munmap in the current kernel])
smu_line=$($GREP -n " sys_munmap" /boot/System.map-$(uname -r) | $SED 's/:.*//')
sys_munmap=$($SED "${smu_line}q;d" /boot/System.map-$(uname -r) | $SED 's/ .*//')
if test -z "$sys_munmap"; then
        AC_MSG_ERROR([Address of sys_munmap not found in kernel map])
fi
AC_MSG_RESULT([found at 0x$sys_munmap])
AC_SUBST([SMU_ADDR], [0x$sys_munmap])

# Are kernel headers installed?
AC_CHECK_HEADERS([linux/ioctl.h],,
       [AC_MSG_ERROR([You must install kernel-headers])])

# Final output
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
