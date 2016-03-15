#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <numaif.h>
#include <numa.h>

#include <unordered_map>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

//#define EARLY_MOVE
//#define SIMULATE_SINGLE_VIEW

#define MAX_THREAD 33
#define ALPHA 0.5
#define ZONES 8
#define CORES_PER_ZONE 4

#define PAGE_COUNT	512
#define PAGE_SIZE	4096
#define MAX_PAGE_COUNT	32*1024*1024

int thread_mapping[MAX_THREAD] = { 0 };
int t_map_count = 0;


typedef struct _thread_access {
	double accesses;
	int thread_id;
	struct _thread_access *next;
} thread_access;


typedef	struct _map_access{
	double accesses[MAX_THREAD][MAX_THREAD] ;
} map_access;



unordered_map<void *, thread_access *> page_map;
unordered_map<void *, map_access *> page_access;


typedef struct _thread_affinity {
	int t1;
	int t2;
	double aff;
} thread_affinity;


vector<thread_affinity *> th_aff;

map_access access_matrix;

int cpu_zone_free[ZONES];
int cpu_zone[ZONES][CORES_PER_ZONE];


static int *numa_nodes;
bool numa_nodes_initialized = false;

void *pages[MAX_PAGE_COUNT];	
int   nodes[MAX_PAGE_COUNT];
int   status[MAX_PAGE_COUNT];
thread_access *t;
int counter_pmove = 0;
void *initial_page;
int ret;



int get_core_of(int thread) {
	int i;
	cpu_set_t cpuset;
	sched_getaffinity(thread, sizeof(cpu_set_t), &cpuset);
	for(i = 0; i < 32; i++) {
		if(CPU_ISSET(i, &cpuset)) {
			printf("thread %d on core %d\n", thread, i);
			return i;
		}
	}
	
}

int get_numa_node(int core) {
	return numa_nodes[core];
}

static int query_numa_node(int id){
        #define NUMA_INFO_FILE "./numa_info"
        #define BUFF_SIZE 1024

        FILE *numa_info;

        char buff[BUFF_SIZE];
        char temp[BUFF_SIZE];

        int i;
        int core_id;
        char* p;

        system("numactl --hardware | grep cpus > numa_info");

        numa_info = fopen(NUMA_INFO_FILE,"r");

        i = 0;
        while( fgets(buff, BUFF_SIZE, numa_info)){
                sprintf(temp,"node %i cpus:",i);

                p = strtok(&buff[strlen(temp)]," ");

                while(p){
                        core_id = strtol(p,NULL, 10);
                        if (core_id == id) 
				return i;
                        p = strtok(NULL," ");
                }
                i++;
        }

	fclose(numa_info);

	unlink("numa_info");
       
        return -1;
	#undef NUMA_INFO_FILE
	#undef BUFF_SIZE
}

static void setup_numa_nodes(void) {

	unsigned int i;
	unsigned int n_cores = sysconf( _SC_NPROCESSORS_ONLN );

	numa_nodes = (int *)malloc(sizeof(int) * n_cores);

	for(i = 0; i < n_cores; i++) {
		numa_nodes[i] = query_numa_node(i);
	}

}



int gimme_thread(int tid) {
	int i;
	for(i = 0; i < MAX_THREAD; i++) {
		if(thread_mapping[i] == tid)
			return i;
	}
	fprintf(stderr, "Thread %d not found\n", tid);
	fflush(stderr);
	abort();
}

int gimme_tid(int thread) {
	return thread_mapping[thread];
}


static inline void set_affinity(int thread, int core) {
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(core, &cpuset);
	// 0 is the current thread
	sched_setaffinity(gimme_tid(thread), sizeof(cpuset), &cpuset);
}


void make_access_matrix(void) {
	int i, j;
	double sum;
	map_access *map;

	for(i = 0; i < MAX_THREAD; i++) {
		for(j = i; j < MAX_THREAD; j++) {
			sum = 0;

			for(auto p : page_access) {
				map = p.second;
				sum += map->accesses[i][j];
			}

			//access_matrix.accesses[i][j] = access_matrix.accesses[j][i] = sum / MAX_THREAD;
			access_matrix.accesses[i][j] = access_matrix.accesses[j][i] = sum;
		}
	}
}


void make_per_page_access_freq(void) {
	thread_access *t, *t_initial;
	void *page;
	map_access *m;

	int tid1, tid2;
	double max, min;

        for(auto i : page_map) {
		page = i.first;
                t_initial = i.second;

		m = (map_access *)malloc(sizeof(map_access));
		t = t_initial->next;

		while(t != NULL) {
//			if(t_initial->accesses > t->accesses) {
				max = t_initial->accesses;
				min = t->accesses;
//			} else {
//				max = t->accesses;
//				min = t_initial->accesses;
//			}

			tid1 = gimme_thread(t_initial->thread_id);
			tid2 = gimme_thread(t->thread_id);

			if( max == 0 || min == 0) {
				m->accesses[tid1][tid2] = m->accesses[tid2][tid1] = 0;
			} else if( (max + min) / 2 > ALPHA) {
				m->accesses[tid1][tid2] = m->accesses[tid2][tid1] = (max + min) / 2;
			}
			//printf("access matrix[%d][%d] is %f\n",tid1,tid2,m->accesses[tid1][tid2]);
			t = t->next;
		}
		t_initial = t_initial->next;

		page_access.insert(std::make_pair(page, m));
		
        }

}



void make_relative_tuples(void) {
	thread_access *t;
	double max = 0;
	thread_access *max_t;
	int j;

	if(!numa_nodes_initialized) {
		numa_nodes_initialized = true;
		setup_numa_nodes();
	}


	counter_pmove = 0;
	for(auto i : page_map) {
		t = i.second;

		while(t != NULL) {
			if(t->accesses > max) {
				max = t->accesses;
				max_t = t;
			}

			t = t->next;
		}

		t = i.second;
		while(t != NULL) {
			t->accesses /= max;
			t = t->next;
		}

#ifdef SIMULATE_SINGLE_VIEW
		t = i.second;
		bool found = false;
		while(t != NULL) {

			if(t->accesses != 0 && found) {
				t->accesses = 0;
			}

			if(t->accesses != 0 && !found) {
				found = true;
			}

			t = t->next;
		}
#endif

#ifdef EARLY_MOVE
		// Immediate remap
		initial_page = (void *)((unsigned long long)i.first * 2 * 1024 * 1024);

		for(j = 0; j < PAGE_COUNT; j++) {
			pages[counter_pmove] = (void *)((char *)initial_page + j * PAGE_SIZE);
			nodes[counter_pmove++] = get_numa_node(get_core_of(max_t->thread_id));
			if(counter_pmove > MAX_PAGE_COUNT) {
				fprintf(stderr, "Too many pages to move\n");
				fflush(stderr);
				abort();
			}
		}
#endif

	}

#ifdef EARLY_MOVE
	numa_move_pages(0, counter_pmove, (void **)pages, nodes, status, MPOL_MF_MOVE);
#endif
}


void parse_line(char *line) {
	float time = 0;
	int code = 0;
	int zone = 0;
	int page = 0;
	int tid = 0;
	int access = 0;

	void *base_page_pointer = 0;

	thread_access *t = 0, *t_prev = 0;

	int i = 0;
	bool t_found = false;

	// [61265.514544] -93- page (68,340) 5496:1

	//  sscanf(const char *str, const char *format, ...);
	sscanf(line, "[%f] -%d- page (%d,%d) %d:%d", &time, &code, &zone, &page, &tid, &access);

//	base_page_pointer = (void *)((unsigned long long)zone * 2 * 1024 * 1024 + (unsigned long long)page);
	base_page_pointer = (void *)(unsigned long long)zone;

	// Int thread mapping;
	i = 0;
	for(i = 0; i < MAX_THREAD; i++) {
		if(thread_mapping[i] == tid) {
			t_found = true;
			break;
		}
	}
	if(!t_found) {
		printf("t_map_count: %d --> %d\n", t_map_count, tid);
		thread_mapping[t_map_count++] = tid;
	}


	auto search = page_map.find(base_page_pointer);
	if(search != page_map.end()) {
		// Page is already present, look for thread. If not found, add the node.
		t = search->second;

		while(t != NULL) {
			if(t->thread_id == tid) {
//				printf("Same thread %d access on page %p\n", tid, base_page_pointer);
				t->accesses += access;
				return;
			}
			t_prev = t;
			t = t->next;
		}

		// No thread found, add at the end;
		t = (thread_access *)malloc(sizeof(thread_access));
                t->accesses = access;
                t->thread_id = tid;
                t->next = NULL;

		t_prev->next = t;

	} else {
		// New page, insert first node
		t = (thread_access *)malloc(sizeof(thread_access));
		t->accesses = access;
		t->thread_id = tid;
		t->next = NULL;

		page_map.insert(std::make_pair(base_page_pointer,t));
	}
}

void parse(void) {
	FILE *f;
	char line[512];
	f = fopen("migration_info", "r");

	page_map.reserve(7000);
	page_access.reserve(7000);


	while (fgets(line, 512, f) != NULL){
		parse_line(line);
	}

	make_relative_tuples();
#ifndef EARLY_MOVE
	make_per_page_access_freq();
	make_access_matrix();
#endif
}


void build_access_matrix(int code) {
	char command[512];

	bzero(&access_matrix, sizeof(access_matrix));
	page_map.clear();
	page_access.clear();

	snprintf(command, 512, "dmesg | grep '\\-%d\\-' > migration_info", code);
//	snprintf(command, 512, "cat one_shot_dmesg_dump/is.dump > migration_info");
	//system(command);
	
	//snprintf(command, 512, "cat %s > migration_info", file);
	printf("%s\n", command);
	system(command);

	parse();
}


void dump_access_matrix(void) {
	int i, j;
	for(i = 0; i < MAX_THREAD; i++) {
		for(j = 0; j < MAX_THREAD; j++) {
			printf("%.04f ", access_matrix.accesses[i][j]);
		}
		puts("");
	}
}



bool sort_thread(thread_affinity *a, thread_affinity *b) {
	return a->aff > b->aff;
}



void remap_threads_and_pages(void) {

	int i, j, k;
	thread_affinity *ta;
	bool first = true;
	bool assigned[MAX_THREAD] = { false };

	for(i = 0; i < MAX_THREAD; i++) {
                for(j = i+1; j < MAX_THREAD; j++) {
			ta = (thread_affinity *)malloc(sizeof(thread_affinity));
			if(ta == NULL) {
				fprintf(stderr, "Malloc returned null\n");
				fflush(stderr);
				abort();
			}
			ta->t1 = i;
			ta->t2 = j;
			ta->aff = access_matrix.accesses[i][j];

			th_aff.push_back(ta);
                }
        }
	sort(th_aff.begin(), th_aff.end(), sort_thread);

	for(i = 0; i < ZONES; i++) {
		cpu_zone_free[i] = 0;
//		for(j = 0; j < CORES_PER_ZONE; j++) {
//			cpu_zone[i][j] = -1;
//		}
	}

	i = 0;
	for(auto el : th_aff) {
//		printf("<%f, %d, %d>\n", el->aff, el->t1, el->t2);

		if(first) {
			cpu_zone_free[i] = 2;
			cpu_zone[i][0] = el->t1;
			cpu_zone[i][1] = el->t2;
			assigned[el->t1] = true;
			assigned[el->t2] = true;
			first = false;
			continue;
		}

		for(j = i; j < ZONES; j++) {
			for(k = 0; k < CORES_PER_ZONE; k++) {
				if(!assigned[el->t2] && cpu_zone_free[j] < CORES_PER_ZONE && (cpu_zone[j][k] == el->t1)) {
					cpu_zone[j][cpu_zone_free[j]++] = el->t2;
					assigned[el->t2] = true;
					goto assigned;
				} else if(!assigned [el->t1] && cpu_zone_free[j] < CORES_PER_ZONE && (cpu_zone[j][k] == el->t2)) {
					cpu_zone[j][cpu_zone_free[j]++] = el->t1;
					assigned[el->t1] = true;
                                        goto assigned;
				}
			}
		}

	     assigned:

		if(cpu_zone_free[i] == CORES_PER_ZONE)
			i++;
	}

	// Leftovers
	for(i = 0; i < MAX_THREAD; i++) {
		if(!assigned[i]) {
			printf("%d not assigned\n", i);
			bool placed = false;
			for(j = i; j < ZONES; j++) {
				if(cpu_zone_free[j] < CORES_PER_ZONE) {
					cpu_zone[j][cpu_zone_free[j]++] = i;
					placed = true;
				}
				if(placed)
					break;
			}
		}
	}

	if(!numa_nodes_initialized) {
		numa_nodes_initialized = true;
		setup_numa_nodes();
	}


	for(i = 0; i < ZONES; i++) {
		printf("Mapping for zone %d: ", i);
		for(k = 0; k < CORES_PER_ZONE; k++) {

			if(i * CORES_PER_ZONE + k == MAX_THREAD)
				goto finished_thread_mapping;

			for ( auto it = page_map.begin(); it != page_map.end(); ++it ) {
				t = it->second;
				while(t != NULL) {
					if(gimme_thread(t->thread_id) == cpu_zone[i][k]) {
						initial_page = (void *)((unsigned long long)it->first * 2 * 1024 * 1024);

						for(j = 0; j < PAGE_COUNT; j++) {
							pages[counter_pmove] = (void *)((char *)initial_page + j * PAGE_SIZE);
							nodes[counter_pmove++] = get_numa_node(i * CORES_PER_ZONE + k);
							if(counter_pmove > MAX_PAGE_COUNT) {
								fprintf(stderr, "Too many pages to move\n");
								fflush(stderr);
								abort();
							}
						}
					}
					t = t->next;
				}
			}

			printf("%d, ", cpu_zone[i][k]);
			set_affinity(cpu_zone[i][k], i * CORES_PER_ZONE + k);

			
		}
		puts("");
	}

    finished_thread_mapping:
	puts("");


	// Move the pages!
	ret = numa_move_pages(0, counter_pmove, (void **)pages, nodes, status, MPOL_MF_MOVE);

//	for (i=0; i<counter_pmove; i++){
//		printf("     details: status[%i] is %d\n",i,status[i]);
//	}	
	
}


void do_remapping(int code) {

	t_map_count = 0;
	bzero(thread_mapping, sizeof(int) * MAX_THREAD);

	build_access_matrix(code);

//	dump_access_matrix();
#ifndef EARLY_MOVE
	remap_threads_and_pages();
#endif
}

