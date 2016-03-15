


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>//debug
	

#define NUM_THREADS 4



int main(int argc, char**argv) {
	
	int i;
	pthread_t tid;
	pthread_t tid_id[NUM_THREADS];
	
	void * addr;
	
	int size = 8*4096;
	char *ptr;

	printf("I'm process %d\n",getpid());
        if (argc < 2 ) {printf("too few arguments\n"); exit(-1);}


	if (argc == 2 && strcmp(argv[1],"munmap") == 0){


repeat_munmap:
			addr =  mmap((void*)0x00000106fffff000,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,0,0);
			printf("mmapping returned addr %p\n",addr);

			ptr = (char *)addr;
			ptr[0]='a';
			ptr += (3*4096);

			sleep(1);

			munmap(addr,size);
	
			goto repeat_munmap;

			
	}

	if (argc == 2 && strcmp(argv[1],"fault") == 0){


			addr =  mmap((void*)0x00000106fffff000,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,0,0);
			printf("mmapping returned addr %p\n",addr);

repeat_fault:

			printf("accessing memory slice at addr %p\n",addr);
			ptr = (char *)addr;
			ptr[0]='a';
			ptr += (3*4096);

			sleep(6);
	
			goto repeat_fault;

			
	}

}

