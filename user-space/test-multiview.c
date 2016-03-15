/**
* this program implements a multi-view based command that receivesa a pid and activates 
* multi-view based page access tracking for the process threads
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
//#include "overtick.h"
//#include "../multi-view/multi-view.h"
#include <mv_ioctl.h>
#include <fcntl.h>
#include <sys/ioctl.h>


#define TP (1) // Trace Period - this is the time interval along which the multi-view facility stays active 
		 //you can modify it at your own pace
		//the suggested value of 1 sec shoudl allow tracking the hottest parts 
		//of each thread working set of pages at reduced overhead

#define AP (5) // Activation Period - this is the period for reactivation of the multi-view facility 
		 //you can modify it at your own pace
		//the suggested value of 10 secs should allow tracking per=thread WS changes 
		//while not inducing significant overhead

long pid;	//the target process (thsi will correspond to argv[1]

//#define DO_REMAPPING //you can trace without actual remapping of pages and threads - just comment this define

/* the below is to call the get_WS system call by passing a univoque time mark as it input code */
#define CLOCK_READ() ({ \
			unsigned int lo; \
			unsigned int hi; \
			__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
			((unsigned long long)hi) << 32 | lo; \
			})


//this points to the function implmenting the WS analysis and affinity computation/actuation steps

#ifdef DO_REMAPPING
extern void do_remapping(int code);
#endif


char dummy[4096];//not actually used - get_WS logs on the dmesg buffer


int get_WS(long int x, char * p, size_t size){
	return syscall(x,p,size);
}


int main(int argc, char **argv) {
	int i;
	int a;
	void * status;
	int ret;
	int fd;
	int count = 0;
	
	int code = 91;


	if (argc<2){
		printf("missing  'arg'\n");
		printf("usage: prog arg\n");
		return -1;
	}


	fd = open("/dev/multi-view", O_RDONLY);

 	if (fd == -1) {
		printf("error opening the multi-view device file\n");
		return -1;
 	}

	pid = strtol(argv[1],NULL,10);
 
restart:

	system("swapoff -a");

	ret = ioctl(fd, IOCTL_SETUP_PID, pid);
	printf("setup pid n multi-view returned value %d\n",ret);

	sleep(TP);

	ret = ioctl(fd, IOCTL_SHUTDOWN_PID, pid);
	printf("shutdown pid returned value %d\n",ret);

retry:
	ret = ioctl(fd, IOCTL_SHUTDOWN_ACK, pid);
	printf("shutdown ack returned value %d\n",ret);
	if(ret == -1){
		sleep(1);
		goto retry;
	
	}
	ret = ioctl(fd, IOCTL_SHUTDOWN_VIEWS, UNLOCK);
	printf("shutdown parallel views returned value %d\n",ret);

//	pause();


//	sleep(20);
//	goto restart;

	code = (int)CLOCK_READ();
	
	get_WS(SYSCALL_NUM, dummy, code);

	system("swapon -a");

#ifdef DO_REMAPPING
	do_remapping(code);
#endif

	sleep(AP);

	goto restart;

	return 0;
}

