/**
*                       Copyright (C) 2008-2013 HPDCS Group
*                       http://www.dis.uniroma1.it/~hpdcs
*
*
* 
* multi-view.h is free software; you can redistribute it and/or modify it under the
* terms of the GNU General Public License as published by the Free Software
* Foundation; either version 3 of the License, or (at your option) any later
* version.
* 
* multi-view.h is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
* A PARTICULAR PURPOSE. See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along with
* this package; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
* 
* @file multi-view.h 
* @brief This is the main source for the Linux Kernel Module which implements
*       per-thread page access tracking - via minor faults - within a mult-view address space
* @author Alessandro Pellegrini
* @author Francesco Quaglia
*
* @date March 4, 2016
*/

// TODO: move to configure.ac
//#define HAVE_LINUX_KERNEL_MAP_MODULE

#ifdef HAVE_LINUX_KERNEL_MAP_MODULE

#pragma once
#ifndef __KERNEL_MEMORY_MAP_MODULE_H
#define __KERNEL_MEMORY_MAP_MODULE_H

#include <linux/ioctl.h>
#include "mv_ioctl.h"

#ifndef HAVE_FAULT_TYPES
#define HAVE_FAULT_TYPES
#include "fault_types.h"
#endif


#define PML4(addr) (((long long)(addr) >> 39) & 0x1ff)
#define PDP(addr)  (((long long)(addr) >> 30) & 0x1ff)
#define PDE(addr)  (((long long)(addr) >> 21) & 0x1ff)
#define PTE(addr)  (((long long)(addr) >> 12) & 0x1ff)

#define OBJECT_TO_PML4(object_id) ((ulong)object_id >> 9 )
#define OBJECT_TO_PDP(object_id) ((ulong)object_id &  0x1ff)

#define GET_ADDRESS(addr)  ( (((long long)(addr)) & ((1LL << 40) - 1)) >> 12)


#define PML4_PLUS_ONE(addr) (void *)((long long)(addr) + (1LL << 39))



#define ENABLE if(1)

/* core user defined parameters */
#define SIBLING_PGD 128 // max number of different memory views within a same address space 
			// you can trace per-thread page faults for up to SIBLING_PGD concurrent threads 
			// if a thread releases its view (e.g. before terminating) then it can be used for
			// a different thread 

#define MV_THREADS (SIBLING_PGD) //max number of concurrent threads running in multi-view mode - this cannot exceed SIBLING_PGD


/*
#define MV_IOCTL_MAGIC 'T'

#define IOCTL_SETUP_PID _IOW(MV_IOCTL_MAGIC, 2, unsigned long ) 
#define IOCTL_SHUTDOWN_PID _IOW(MV_IOCTL_MAGIC, 3, unsigned long ) 
#define IOCTL_SHUTDOWN_ACK _IOW(MV_IOCTL_MAGIC, 4, unsigned long ) 
#define IOCTL_SHUTDOWN_VIEWS _IOW(MV_IOCTL_MAGIC, 5, unsigned long ) 
*/


#endif /* __KERNEL_MEMORY_MAP_MODULE_H */
#endif /* HAVE_LINUX_KERNEL_MAP_MODULE */


