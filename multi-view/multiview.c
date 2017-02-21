/*                       Copyright (C) 2008-2013 HPDCS Group
*                       http://www.dis.uniroma1.it/~hpdcs
*
*
* multi-view.c is free software; you can redistribute it and/or modify it under the
* terms of the GNU General Public License as published by the Free Software
* Foundation; either version 3 of the License, or (at your option) any later
* version.
* 
* multi-view.c is distributed in the hope that it will be useful, but WITHOUT ANY
* WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
* A PARTICULAR PURPOSE. See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License along with
* this package if not, write to the Free Software Foundation, Inc.,
* 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
* 
* @file multi-view.c 
* @brief This is the main source for the Linux Kernel Module which implements
*	per-thread page access tracking - via minor faults - within a mult-view address space
*
* @author Alessandro Pellegrini
* @author Francesco Quaglia
*
* @date March 4, 2016
*/

#define HAVE_LINUX_KERNEL_MAP_MODULE
#ifdef HAVE_LINUX_KERNEL_MAP_MODULE
#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/spinlock_types.h>
#include <linux/spinlock.h>
#include <asm/tlbflush.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include "multi-view.h"
#include "tracking_accesses.h"


#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,25)
#error Unsupported Kernel Version
#endif
/* FUNCTION PROTOTYPES */


static int multi_view_init(void);
static void multi_view_cleanup(void);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Pellegrini <pellegrini@dis.uniroma1.it>, Francesco Quaglia <quaglia@dis.uniroma1.it>");
MODULE_DESCRIPTION("Per thread page fault tracker within a multi-view address space");
module_init(multi_view_init);
module_exit(multi_view_cleanup);

#define MV_FAULT_DEBUG if(1)

#define DEBUG if(1)

#define SCHED_DEBUG if(1)

#define DEBUG_SCHEDULER_HOOK if(1)


#define SKIP_ENTRY  200 //this parameter determines how many entries of the PML4 (starting from its beginning) 
			//are subject to multi-view management
			//it MUST not exceed 512 (the PML4 size in terms of its entries) 
			//keeping it lower than 256 allows for tracking per-thread accessed 
			//within the address space by still avoiding minor faults on, e.g., kernel stuff or VDSO
			//you could any how modify this value depending on your own needs


static long multi_view_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int multi_view_release(struct inode *inode, struct file *filp);
int multi_view_open(struct inode *inode, struct file *filp);

/* File operations for the multi-vew device file  */
struct file_operations fops = {
        open:           multi_view_open,
        unlocked_ioctl: multi_view_ioctl,
        compat_ioctl:   multi_view_ioctl, // Nothing strange is passed, so 32 bits programs should work out of the box
        release:        multi_view_release
};

/* MODULE VARIABLES */

//PID of the process whose threads need to be traced
long target_process = -1;
long old_target_process = -1;

//core multi-view metadata
void *pgd_addr[MV_THREADS];
struct mm_struct *mm_struct_addr[MV_THREADS];
unsigned int managed_pgds = 0;

//meta data table and locks for thread registration/management within the multi-view device file
static DEFINE_MUTEX(ts_thread_register);
static int ts_threads[MV_THREADS]={[0 ... (MV_THREADS-1)] = -1};
spinlock_t lock[MV_THREADS]; //set at run time
int enabled_registering = 0;


//management of system calls
int restore[2] = {[0 ... 1] -1};
int to_restore_sys_call = 0;


/* please take the two values below from the system map */
unsigned long sys_call_table = SCT_ADDR;
unsigned long sys_ni_syscall_addr = SNS_ADDR; //0xffffffff8107e720; //0xffffffff8107e700;
asmlinkage unsigned long (*sys_munmap_addr)(void *addr, size_t length) = SMU_ADDR;

void release_sibling_view(void** pgd_entry);
void catch_pte_fault(void * fault_address);

asmlinkage long sys_munmap_wrapper(void *addr, size_t length){

	int t;
	long ret;
        void** pgd_entry;

//	DEBUG
//       printk("%s: this is the wrapper for sys_munmap to enable safe excution with sigling memory views\n", KBUILD_MODNAME);

	DEBUG{
		if (current->tgid == target_process)
 	       		printk("%s: watchdogged process - a thread issued sys_munmap\n", KBUILD_MODNAME);
	}


	if(current->tgid == target_process){
		goto wrapped_path;
	}

	if(current->tgid == old_target_process){
		goto wrapped_path;
	}

	goto regular_munmap;

wrapped_path:

	DEBUG
       	printk("%s: this is the wrapper for sys_munmap to enable safe excution with sigling memory views\n", KBUILD_MODNAME);

	for (t = 0; t < MV_THREADS; t++) {

                        spin_lock(&lock[t]);

                        pgd_entry = (void**)pgd_addr[t];

                        release_sibling_view(pgd_entry);
        }

	
        ret = sys_munmap_addr(addr, length);

	//need to unlock the sibling views here
	for(t=0; t<MV_THREADS; t++){
	
		spin_unlock(&lock[t]);

	}

	return ret;

regular_munmap:

        ret = sys_munmap_addr(addr, length);

	return ret;

}


//module parameters
void (*the_hook)(void);
module_param(the_hook,ulong,0774);

int dev_file_major;
module_param(dev_file_major,int,0600);

//device file mutex - for single instance management
static DEFINE_MUTEX(pgd_get_mutex);

static ulong watch_dog=0;

int syscall_number;
module_param(syscall_number,int,0444);


//device file metadata
static DEFINE_MUTEX(ts_mutex);
static int major;




//native code for update of CR3
static inline void my_native_write_cr3(unsigned long val)
{
          asm volatile("mov %0,%%cr3": : "r" (val), "m" (__force_order));
}

//native code for reading CR2
static inline unsigned long my_native_read_cr2(void)
{
         unsigned long val;
         asm volatile("mov %%cr2,%0\n\t" : "=r" (val), "=m" (__force_order));
         return val;
}

void release_sibling_view(void** pgd_entry){

	int i,j,k,t;
        unsigned long control_bits;
        void* address;
        void ** my_pdp;
        void ** my_pd;
        void ** my_pde;
        void ** my_pte;
        void ** aux;

	for (i=0; i<SKIP_ENTRY; i++){

       		 if(pgd_entry[i] != NULL){

                           address = pgd_entry[i];
                           my_pdp = (void**)(__va((ulong)address & 0xfffffffffffff000));

			/* going through PDP level */
                           for(j=0;j<512;j++){

                               if(my_pdp[j] != NULL){

                                         address = my_pdp[j];
                                         my_pde = (void**)(__va((ulong)address & 0xfffffffffffff000));

					/* going through PDE level */
                                         for(k=0;k<512;k++){

						if(my_pde[k] != NULL){
							//check on large page
							if( ((ulong)my_pde[k] & 0x081) ){
							/* just reset -freeing here corrupts the original view */
								my_pde[k] = NULL;
							}else{

                                                              	address = my_pde[k];
                                                               	my_pte = (void**)(__va((ulong)address & 0xfffffffffffff000));

                                                               	SCHED_DEBUG
                                                               	printk("freeing PTE indexed (%d,%d,%d)\n",i,j,k);

									//the below order is requeted for linearizability with 
									//page table firmware access 
								memset(address,0,4096);
                                                       		my_pde[k] = NULL;
								free_page(address);
							}
									
                                               	 }

                                          }//end for k

                                          SCHED_DEBUG
                                          printk("freeing PDE indexed (%d,%d)\n",i,j);

					/* linearizability with page table firmware access */
					//aux = my_pdp;
					  memset(my_pde,0,4096);
                                          my_pdp[j] = NULL;
                                          free_page(my_pde);
                                    }

                                }//end for j

				DEBUG
                                printk("%s: freeing PDP indexed %d\n", KBUILD_MODNAME, i);

                                memset(my_pdp,0,4096);
                                pgd_entry[i] = NULL;
                                free_page(my_pdp);
                        }// end if 

                     }//end for i

			//just reset the sibling PML4 entries possibly pointing to original view stuff 
		     for (i = SKIP_ENTRY; i< 512; i++){
			
			pgd_entry[i] = NULL;

		     }

}//end of release sibling view



int multi_view_open(struct inode *inode, struct file *filp) {

	int i;

        // It's meaningless to open this device in write mode
        if (((filp->f_flags & O_ACCMODE) == O_WRONLY)
            || ((filp->f_flags & O_ACCMODE) == O_RDWR)) {
                return -EACCES;
        }

        // this device file is single instance 
        if (!mutex_trylock(&ts_mutex)) {
                return -EBUSY;
        }

	for (i = 0; i < MV_THREADS; i++) {
                        if(ts_threads[i] != -1) {
			        mutex_unlock(&ts_mutex);
				return -EBUSY;	
			}
        }

        enabled_registering = 1; //now threads of some process can register for multi-view management

        return 0;

}// end multi_view_open

int multi_view_release(struct inode *inode, struct file *filp) {

        mutex_lock(&ts_thread_register);

	old_target_process = target_process;//keep memory for asynchronous thread removal from the device file
        target_process = -1;

        enabled_registering = 0;

        mutex_unlock(&ts_thread_register);

        mutex_unlock(&ts_mutex);

        return 0;

}// end multi_view_release


static long multi_view_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

        int i,t;
        void** pgd_entry;
        void**ancestor_pml4;
        unsigned long control_bits;
        void* address;
//        void ** my_pdp;
//        void ** ancestor_pd;
//        void ** my_pd;
//        void ** my_pde;
//        void ** my_pte;
//        void ** aux;


        switch (cmd) {


        case IOCTL_SETUP_PID:

                DEBUG
                printk("%s: setting up the PID to trace: %lu\n",KBUILD_MODNAME,arg);

                if (arg <= 0) return -EINVAL;

                mutex_lock(&ts_thread_register);

		//cannot setup a different PID if some thread (running or waiting) of another process 
		//is still in multi-view mode
		//we simply tell the device is currently busy

                for (i = 0; i < MV_THREADS; i++) {
                        if(ts_threads[i] != -1){
				mutex_unlock(&ts_thread_register);
				return -EBUSY;
			}
                }

                target_process = arg;

                mutex_unlock(&ts_thread_register);
			
		return 0;

                break;

        case IOCTL_SHUTDOWN_PID:

                DEBUG
                printk("%s: shutting down the raced PID: %lu\n",KBUILD_MODNAME,arg);

                if (arg <= 0) return -EINVAL;

                mutex_lock(&ts_thread_register);

		old_target_process = target_process;//keep memory for asynchronous thread removal from the device file
                target_process = -1; // add a check on whether PID corresponds to target_process

                mutex_unlock(&ts_thread_register);

		return 0;

                break;


        case IOCTL_SHUTDOWN_ACK:

                DEBUG
                printk("checking whether tracing shutdown has been executed: %d\n",arg);

                mutex_lock(&ts_thread_register);

                if (!(target_process == -1)) goto negative_ack;

                for (i = 0; i < MV_THREADS; i++) {
                        if(ts_threads[i] != -1) goto negative_ack;
                }

                mutex_unlock(&ts_thread_register);

                DEBUG
                printk("%s: memory access tracing shutdown successful\n",KBUILD_MODNAME);

                return 0;

negative_ack:
                mutex_unlock(&ts_thread_register);
                DEBUG
                printk("%s: memory access tracing shutdown failure - need to iterate the shutdown command\n",KBUILD_MODNAME);

                return -EBUSY;

		break;

        case IOCTL_SHUTDOWN_VIEWS:

               DEBUG
               printk("%s: shutting down sibling views - arg is: %d (UNLOCK FLAG is %d)\n", KBUILD_MODNAME, arg, UNLOCK);

                for (t = 0; t < MV_THREADS; t++) {

			spin_lock(&lock[t]);

                        pgd_entry = (void**)pgd_addr[t];
			
			release_sibling_view(pgd_entry);
		}

       	        for (t = 0; t < MV_THREADS; t++) {
	
			spin_unlock(&lock[t]);

		}

shutdown_views_done:

		return 0;
                break;

            default:

		return -EINVAL;
		break;
        }

}



static void scheduler_hook(void) {

        int i,j,k;
        int arg;
        void** pgd_entry;
        void**ancestor_pml4;
        void*control_bits;
        void* address;
        void ** my_pdp;
        void** ancestor_pdp;
        void ** ancestor_pd;
        void ** my_pd;

        watch_dog++; //no need to be atomic - it's just periodic audit

        DEBUG_SCHEDULER_HOOK
        if (watch_dog >= 0x00000000000ffff){
                printk(KERN_DEBUG "%s: watch dog trigger for thread %d (group leader is %d) CPU-id is %d\n", KBUILD_MODNAME, current->pid, current->tgid, smp_processor_id());
                watch_dog = 0;
        }


        if(current->mm == NULL) goto hook_end; //no user space memory for the current thread

        if( current->tgid != target_process ){//process not (or no more) watchdogged

                for(i = 0; i < MV_THREADS; i++){//still need to check is the thread was watchdogged

                        if(ts_threads[i] == current->pid){//current thread was watchdogged

                                DEBUG_SCHEDULER_HOOK
                                printk(KERN_INFO "%s: found TS thread %d on CPU %d deregistering \n", KBUILD_MODNAME, current->pid,smp_processor_id());

ENABLE                          my_native_write_cr3(__pa(current->mm->pgd));
				/* move back to the original memory view */

				/* reset control information - thread deregistering from the multi-view device file */
                                //original_view[i] = NULL;
                                ts_threads[i] = -1;

                                goto hook_end;

                        }
                }//end for

                goto hook_end;
        }

        if( current->mm != NULL && current->tgid == target_process ){
		/* a thread has been scheduled running within a currently watchdogged process */

                DEBUG_SCHEDULER_HOOK
                printk("Thread %d lives in traced process %d\n",current->pid,current->tgid);

                for(i = 0; i < MV_THREADS; i++){  

                        if(ts_threads[i] == current->pid){
				/* the thread is already running on a sibling memory view */

                                DEBUG_SCHEDULER_HOOK
                                printk(KERN_INFO "%s: found TS thread %d on CPU %d\n", KBUILD_MODNAME, current->pid,smp_processor_id());
ENABLE                          my_native_write_cr3(__pa(pgd_addr[i])); 
				/* thread move on the sibling view upon reschedule */

                                goto hook_end;
                        }
                }//end for

        } // end if 

        if( current->mm != NULL && current->tgid == target_process) {
		/* need to register the thread within the multi-view special device file */
		/* given that the whole process is subject to memory access tracing */

                DEBUG_SCHEDULER_HOOK
                printk("%s: need to register thread %d\n", KBUILD_MODNAME, current->pid);

                mutex_lock(&pgd_get_mutex);

                if (current->tgid != target_process){
			/* concurrent deregistration of the watchdogged process - skip registering the thread*/

                        mutex_unlock(&pgd_get_mutex);
                        goto hook_end;
                }

                for (i = 0; i < MV_THREADS; i++) {

                        if (ts_threads[i] == -1) {
                                ts_threads[i] = current->pid;
                                arg = i;
                                memcpy((void *)pgd_addr[i], (void *)(current->mm->pgd), 4096); 
					/* prefill of the sibling PML4 */
					/* page accesses via minor faults will be traced by initially relying on PDP tables*/
                                //original_view[i] = current->mm;
                                goto pgd_get_done;
                        }/*end if*/

                }/*end for*/
		
                mutex_unlock(&pgd_get_mutex);

		goto hook_end;
		/* no more room in the multi-view device file  (no more available sibling PGDs) */
		/* the thread will not leave on a sibling memory view, rather on the original one */
		/* its page accesses will not be traced within the multi-view address space */

   pgd_get_done:
                mutex_unlock(&pgd_get_mutex);

                DEBUG_SCHEDULER_HOOK
                printk("%s: thread %d registered in slot %d\n",KBUILD_MODNAME,current->pid,arg);

                pgd_entry = (void**)pgd_addr[arg];
                ancestor_pml4 = (void**)current->mm->pgd;

                DEBUG_SCHEDULER_HOOK
                printk("%s: threads registered has ancestor set to %p - original pgd set to %p\n", KBUILD_MODNAME, ancestor_pml4, current->mm->pgd);

		//need to lock the sigling memory view 
		spin_lock(&lock[arg]);

                for (i=0; i<SKIP_ENTRY; i++){
			/* this cycle preallocates sibling PDP tables to avoid successive faults at PDP level for already valid memory */

                         if(ancestor_pml4[i] != NULL){

                            control_bits = (ulong)ancestor_pml4[i] & 0x0000000000000fff;

                             address=(void *)get_zeroed_page(GFP_KERNEL);

                                    if(address == NULL){
                                         printk("%s: bad address for PDP allocation (i = %d)\n", KBUILD_MODNAME, i);
                                    }
                                    else{
                                         SCHED_DEBUG
                                         printk("%s: good address for PDP allocation (i = %d - addr is %p)\n", KBUILD_MODNAME, i,address);
                                    }

                              address = __pa((ulong)address);
                              address = (ulong) address | (ulong) control_bits;


                              pgd_entry[i]=address;

                              my_pdp = (void**)(__va((ulong)pgd_entry[i] & 0xfffffffffffff000));
                              ancestor_pdp =(void **) __va((ulong) ancestor_pml4[i] & 0xfffffffffffff000);

                              for(j=0; j<512; j++) {
				/* this cycle preallocates sibling PDE tables to avoid on demand allocationy */

                                        if(ancestor_pdp[j] != NULL) {

                                            control_bits = (ulong) ancestor_pdp[j] & 0x0000000000000fff;

                                            address=get_zeroed_page(GFP_KERNEL);

                                            if(address == NULL){
                                                 printk("bad address for PDE allocation (j = %d)\n",j);
                                            }
                                            else{
                                                SCHED_DEBUG
                                                printk("got address for PDP allocation (j = %d - addr is %p)\n",j,address);
                                            }
                                            address=__pa((ulong)address);
					    address = (ulong) address | (ulong) control_bits;
                                            my_pdp[j]=address;

                                             ancestor_pd = (void **) __va((ulong) ancestor_pdp[j] & 0xfffffffffffff000);
                                             my_pd = (void **) __va((ulong) my_pdp[j] & 0xfffffffffffff000);


                                            for(k=0; k<512; k++){
						/* PDE entries are set to NULL to be able to trace accesses to large/regular pages */
                                                   if(ancestor_pd[k]!=NULL){
                                                        DEBUG_SCHEDULER_HOOK
                                                        printk("%s: (%d,%d) found ancestor[%d] set to %p\n",KBUILD_MODNAME,i,j,k,ancestor_pd[k]);
                                                    }//end if
                                             }//enf for k

                                         }//end if ancestor_pdp

                            }//end for j

                         }//end if ancestor_pml4

                     }//end for i

        }

        DEBUG_SCHEDULER_HOOK
        printk("%s: thread %d - done sibling view setup \n", KBUILD_MODNAME, current->pid);
		
	/* we now move to the sibling view */
ENABLE  my_native_write_cr3(__pa(pgd_addr[arg]));

	//releasing the view for cocnurrent updates (e.g. munmap)
	spin_unlock(&lock[arg]);

    hook_end:

        return;

}




asmlinkage long sys_get_WS(char* dummy, int code ){

	int ret;
	
	printk(KERN_INFO "%s: sys_get_WS has been called with params  %p - %d\n",KBUILD_MODNAME,dummy,(int)code);

	audit_hashbucket(code);//this flushes logged data (traced accesses) to dmesg buffer - code is an incarnation number

	clean_hashbucket();//this resets the tracing data structure

	return 0;

//bad_addr://direct passage of data to user space buffer not yet supported - dummy address not actually checked

	return -EINVAL;
}



/**************************
* multi_view_page_fault is a wrapper for the orignal kernel page fault handler
* it checks whether the original handler needs to be invoked
* or the memory fault is only caused by multi-view setup and page access tracing
* this function takes also care of realigning the sibling memory view of the faulting thread to the
* original memory view 
*************************/

int multi_view_page_fault(struct pt_regs* regs, long error_code, long realign_mode){
 	void* target_address;
	void ** my_pgd;
	void ** my_pdp;
    	void ** my_pte;
	void** ancestor_pdp;
	ulong i;
    	void ** my_pd;
    	void ** ancestor_pd;
    	void ** ancestor_pte;
    	void * address;
    	void ** ancestor_pml4;
	int large_page = 0;



	/* this thread - has no user space image  */
	if(current->mm == NULL) return NORMAL_FAULT;  
	
	/* take the faulting address */       
	target_address = (void*)my_native_read_cr2(); 
 
	for(i=0;i<MV_THREADS;i++){

			/* check whether the thread is currently watchdogged */
		if ((ts_threads[i])==(current->pid)){	
                        
			MV_FAULT_DEBUG{
                        printk("---------------------------- MVPF HANDLER -------------------------\n");
                        printk("process found (pid is %d) - relign mode is %d\n",current->pid,realign_mode);
                        printk("current->mm->pgd=%p - pgd_addr[%d]=%p - cr3=%p\n",current->mm->pgd,i,pgd_addr[i],__va(read_cr3()));
                        printk("page-fault address=%p - error code is %p\n",target_address, error_code );
                        printk("tables indexes are: %d - %d - %d - %d\n",PML4(target_address),PDP(target_address),PDE(target_address),PTE(target_address));
			}

			ancestor_pml4 = (void**)current->mm->pgd;
                        
			MV_FAULT_DEBUG{
			printk("fault analysis\n");
			printk("PML4 entry is %p\n",ancestor_pml4[PML4(target_address)]);
			}

			if(ancestor_pml4[PML4(target_address)] == NULL){
				printk("PF handler is returning %d - PML4 miss on entry %d\n",WATCHDOG_NORMAL_FAULT,PML4(target_address));
				return WATCHDOG_NORMAL_FAULT;
			}
			
			
		    	ancestor_pdp = __va((ulong)ancestor_pml4[PML4(target_address)] & 0xfffffffffffff000); 		

			MV_FAULT_DEBUG
			printk("PDP entry is %p\n",ancestor_pdp[PDP(target_address)]);

			if(ancestor_pdp[PDP(target_address)] == NULL){ 
				MV_FAULT_DEBUG
				printk("PF handler is returning %d - PDP miss on entry %d\n",WATCHDOG_NORMAL_FAULT,PDP(target_address)); 
				return WATCHDOG_NORMAL_FAULT;
			}
			//empty zero memory fault - pass it to the classical do_page_fault handler

		    	ancestor_pd = __va((ulong)ancestor_pdp[PDP(target_address)] & 0xfffffffffffff000); 		

			MV_FAULT_DEBUG
			printk("PDE entry is %p\n",ancestor_pd[PDE(target_address)]);

			if(ancestor_pd[PDE(target_address)] == NULL){ 

				MV_FAULT_DEBUG
				printk("PF handler is returning %d - PDE miss on entry %d\n",WATCHDOG_NORMAL_FAULT,PDE(target_address)); 
				return WATCHDOG_NORMAL_FAULT;
			}
			//empty zero memory fault - pass it to the classical do_page_fault handler
            catch_pte_fault((void *) target_address);//my_patch

			if( ((ulong)ancestor_pd[PDE(target_address)] & 0x081) ){

				MV_FAULT_DEBUG
				printk("large page found\n");
				large_page = 1;
				goto ws_page_fault;

			}

		    	ancestor_pte = __va((ulong)ancestor_pd[PDE(target_address)] & 0xfffffffffffff000); 		

			MV_FAULT_DEBUG
			printk("PTE entry is %p\n",ancestor_pte[PTE(target_address)]);

		    	// REDUNDANT SEE BEFORE --- ancestor_pte = __va((ulong)ancestor_pd[PDE(target_address)] & 0xfffffffffffff000); 		
			if(ancestor_pte[PTE(target_address)] == NULL || (((ulong)ancestor_pte[PTE(target_address)] & 0x1) == 0x0)){

				MV_FAULT_DEBUG
				printk("PF handler is returning %d - PTE miss on entry %d\n",WATCHDOG_NORMAL_FAULT,PTE(target_address)); 
				return WATCHDOG_NORMAL_FAULT;
			}


ws_page_fault:
			MV_FAULT_DEBUG
			printk("this might not be a true huge-page fault - might need to update locality metadata (double check for this)!!\n");

			// any operation on the sibling memory view needs to be atomic
			//for safety with restrictions/chances on the original memory view
			spin_lock(&lock[i]);


			//take the pointer to the sibling view top level page table
			my_pgd =(void**) pgd_addr[i];

			MV_FAULT_DEBUG
			printk("sibling PML4 is %p\n",my_pgd[PML4(target_address)]);

			if(my_pgd[PML4(target_address)] == NULL){
			 //allocate the PDP
				my_pdp = get_zeroed_page(GFP_KERNEL);
				address = __pa(my_pdp);
				address = (ulong)address | ((ulong)ancestor_pml4[PML4(target_address)] & 0x0000000000000fff);
				my_pgd[PML4(target_address)] = address;
				//double_check_not_original = 1;

				MV_FAULT_DEBUG
				printk("sanity check (internal) on my_pdp address (physical): %p vs %p\n",(ulong)__pa(my_pdp),((ulong)my_pgd[PML4(target_address)] & 0xfffffffffffff000));
			} 
			

		    	my_pdp = __va((ulong)my_pgd[PML4(target_address)] & 0xfffffffffffff000); 		

			MV_FAULT_DEBUG
			printk("sibling PDP is %p\n",my_pdp[PDP(target_address)]);

			if(my_pdp[PDP(target_address)] == NULL){
			//allocate the PD
				my_pd = get_zeroed_page(GFP_KERNEL);
				address = __pa(my_pd);
				address = (ulong)address | ((ulong)ancestor_pdp[PDP(target_address)] & 0x0000000000000fff);
				my_pdp[PDP(target_address)] = address;

				MV_FAULT_DEBUG
				printk("sanity check (internal) on my_pd address: %p vs %p\n",my_pd,__va((ulong)my_pdp[PDP(target_address)] & 0xfffffffffffff000));

			} 

		    	my_pd = __va((ulong)my_pdp[PDP(target_address)] & 0xfffffffffffff000); 		

			MV_FAULT_DEBUG
			printk("sibling PDE is %p\n",my_pd[PDE(target_address)]);

			if((ulong)my_pd[PDE(target_address)] == NULL ){

				MV_FAULT_DEBUG
				printk("before - sibling PDE is %p\n",my_pd[PDE(target_address)]);

				if(large_page){
					// the below assignment shoudl take palce only for large pages
					 my_pd[PDE(target_address)] = ancestor_pd[PDE(target_address)];
				}
				else{ 

					// allocate sibling PTE and update

					my_pte = get_zeroed_page(GFP_KERNEL);
					address = __pa(my_pte);
					address = (ulong)address | ((ulong)ancestor_pd[PDE(target_address)] & 0x0000000000000fff);
					my_pd[PDE(target_address)] = address;

					//set the sibling PTE entry
					my_pte[PTE(target_address)] = ancestor_pte[PTE(target_address)];
				}

				MV_FAULT_DEBUG
				printk("after - sibling PDE is %p\n",my_pd[PDE(target_address)]);
				//double_check_not_original = 1;

			} 
			else{
				//not a large page - simply relign the sibling PTE entry
				if(!large_page){
					
		    			ancestor_pte = __va((ulong)ancestor_pd[PDE(target_address)] & 0xfffffffffffff000); 
		    			my_pte = __va((ulong)my_pd[PDE(target_address)] & 0xfffffffffffff000); 		

					//here we set the entry
					my_pte[PTE(target_address)] = ancestor_pte[PTE(target_address)];
					
				}
			}

			MV_FAULT_DEBUG
			printk("this fault is caused by multiviews - about to return %d\n", WATCHDOG_FICTITIOUS_FAULT);
            //catch_pte_fault((void *) target_address);//my_patch

			//release sibling view lock
			spin_unlock(&lock[i]);

			//this update does not need to be included in the sibling view management critical section
			//the hash-bucket manager has its own internal synchronization mechanisms
			update_hashbucket((void *) target_address,(unsigned char)i,current->pid);


			return WATCHDOG_FICTITIOUS_FAULT;
                        
		}/*end if*/ 

	}/*end for*/

	return NORMAL_FAULT; //user level thread not under watchdog
}

//this required for cross compilation with the intercept module that redirects page fault handling
//to the multi_view_page_falut wrapper
EXPORT_SYMBOL(multi_view_page_fault);

void catch_pte_fault(void * fault_address)
{
	void ** ancestor_pml4;
	void ** ancestor_pdp;
	void ** ancestor_pd;
	void ** ancestor_page;

	int i = 0;
	ancestor_pml4 = (void**)current->mm->pgd;
	ancestor_pdp = __va((ulong)ancestor_pml4[PML4(fault_address)] & 0xfffffffffffff000); 		
	ancestor_pd = __va((ulong)ancestor_pdp[PDP(fault_address)] & 0xfffffffffffff000); 		

    printk("catch_pte_fault fault addr %p\n", fault_address);
	printk("catch_pte_fault PML4 entry is %p\n",ancestor_pml4[PML4(fault_address)]);
	printk("catch_pte_fault PDP entry is %p\n",ancestor_pdp[PDP(fault_address)]);
	printk("catch_pte_fault PDE entry is %p\n",ancestor_pd[PDE(fault_address)]);

    printk("catch_pte_fault tables indexes are: %d - %d - %d - %d\n",PML4(fault_address),PDP(fault_address),PDE(fault_address),PTE(fault_address));


    for ( i = 0; i < PTE(fault_address); ++i)
    {
		ancestor_page = __va((ulong)ancestor_pd[i] & 0xfffffffffffff000); 
		printk("catch_pte pagina value %p\n",ancestor_page[i]);		
    }

    return;


}

/******************************
* any setup stuff for operations by the multi-view device file
* is here
******************************/

static int multi_view_init(void) {

        int ret;
        int i,j;

	unsigned long * p = (unsigned long *) sys_call_table;

	unsigned long cr0;


	/* finding room for a new sys call - the one flushing WS data to the dmesg buffer */
	j = -1;
	for (i=0; i < 350; i++){
		if (p[i] == sys_ni_syscall_addr){
			printk("%s: sys call table entry %d keeps address %p - installing new sys call here\n", KBUILD_MODNAME, i,(void*)p[i]);
			j++;
			restore[j] = i;
			syscall_number = i;
			if (j == 0) break;
		}
	}

	if (j == -1){
		printk("%s: no room found in sys_call_table - installation of new sys call get_WS failure\n", KBUILD_MODNAME);
		printk("%s: module installation failure\n", KBUILD_MODNAME);
		
		return -1;
	}

	/* finding sys_munmap  - j is set by previous hack */
	for (i=0; i < 350; i++){
		if (p[i] == (long long)sys_munmap_addr){
			printk("%s: sys call table entry %d keeps munmap address %p  wrapping here\n", KBUILD_MODNAME, i,(void*)p[i]);
			j++;
			restore[j] = i;
			if (j == 1) break;
		}
	}

	if (j == 0){
		printk("%s: address of sys_unmap not found in sys_call_table\n", KBUILD_MODNAME);
		printk("%s: module installation failure\n", KBUILD_MODNAME);
		
		return -1;
	}
	

	// setup locks for multi-views management (e.g. munmap)
	for(i=0; i<MV_THREADS; i++){
		spin_lock_init(&lock[i]);
	}

	// expose the function pointer for hooking in /sys 
	the_hook = scheduler_hook;

	// Initialize the device file mutex
        mutex_init(&ts_mutex);

        // Dynamically allocate a major for the device
        major = register_chrdev(0, "multi-view", &fops);
        if (major < 0) {
                printk(KERN_ERR "%s: failed to register device. Error %d\n", KBUILD_MODNAME, major);
                ret = major;
                goto failed_devreg;
        }

        printk(KERN_ERR "%s: multi-view device file has been assigned major %d\n", KBUILD_MODNAME, major);
	dev_file_major = major; //this leads to exposing the major in /sys

	// allocate minimal data for sibling views
        for (i = 0; i < MV_THREADS; i++) {
                //original_view[i] = NULL;
                if ( ! (mm_struct_addr[i] = kmalloc(sizeof(struct mm_struct), GFP_KERNEL)))
                        goto bad_alloc;
                if (!(pgd_addr[i] = (void *)get_zeroed_page(GFP_KERNEL))) {
                        kfree(mm_struct_addr[i]);
                        goto bad_alloc;
                }
                mm_struct_addr[i]->pgd = pgd_addr[i];
                if ((void *)pgd_addr[i] != (void *)((struct mm_struct *)mm_struct_addr[i])->pgd) {
                        printk("bad referencing between mm_struct and pgd\n");
                        goto bad_alloc;
                }
                managed_pgds++;
        }

        printk(KERN_INFO "%s: correctly allocated %d sibling PGDs\n", KBUILD_MODNAME, managed_pgds);

	DEBUG	
	for(i=0;i<MV_THREADS; i++) printk("%s: sibling %d is at address %p\n",KBUILD_MODNAME, i,pgd_addr[i]);


	// sys call table hacking 
	to_restore_sys_call = 1;
	cr0 = read_cr0();
        write_cr0(cr0 & ~X86_CR0_WP);
	p[restore[0]] = (unsigned long)sys_get_WS;
	p[restore[1]] = (unsigned long)sys_munmap_wrapper;
	write_cr0(cr0);

	printk(KERN_ERR "%s: module mounted\n", KBUILD_MODNAME);

        return 0;

    bad_alloc:
	ret = -1;
        printk(KERN_ERR "%s: something wrong while preallocating PGDs or after (here we should deallocate stuff, if any)\n",KBUILD_MODNAME);


    failed_devreg:
        unregister_chrdev(major, "multi-view");
	
        return ret;

}


static void multi_view_cleanup(void) {

	unsigned long * p = (unsigned long *) sys_call_table;

	unsigned long cr0;

	if (to_restore_sys_call){

		cr0 = read_cr0();
       		write_cr0(cr0 & ~X86_CR0_WP);
		p[restore[0]] = (long long)sys_ni_syscall_addr;
		p[restore[1]] = (long long)sys_munmap_addr;
		write_cr0(cr0);
		printk("%s: sys-call table restored to its original content\n", KBUILD_MODNAME);

	}

	for (; managed_pgds > 0; managed_pgds--) {
		__free_pages((void*)mm_struct_addr[managed_pgds-1]->pgd,0); //free di mm_struct_addr[managed_pgds -1]->pgd,0) perchèra un BUG
		kfree(mm_struct_addr[managed_pgds-1]);
	}
	

	/* unregister multi-view char-dev */
        unregister_chrdev(major, "multi-view");

        printk(KERN_INFO "%s: unmounted successfully\n", KBUILD_MODNAME);

}

#endif	/* HAVE_LINUX_KERNEL_MAP_MODULE */

