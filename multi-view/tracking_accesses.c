#include "tracking_accesses.h"
#include <asm/bitops.h>
#include <asm/cmpxchg.h>
#include <linux/slab.h>

#define SIBLING_PGD 128

int PIDs[SIBLING_PGD]={[0 ... (SIBLING_PGD-1)] = -1};

#define TRACER_DEBUG if(0)

//structure
bucket_data buckets[NBUCKETS];

//update_structure
void update_hashbucket(void * fault_address,unsigned char pgd_index, int pid){
			

	PIDs[(int)pgd_index] = pid; // log the ocrrespondence between the mempory view and the PID of the faulting tread

	unsigned int bucket_index = HASH_FUNCTION(fault_address);
	#define bucket (buckets[bucket_index])
	while(cmpxchg(&(bucket.spinlock),0,1)); //while(lock==1)==while(locked)

	TRACER_DEBUG{
	printk("bucket_index=%d\n",bucket_index);
	printk("key_zone=%d\n",ZONE2M(fault_address));
	printk("pte=%d\n",PTE(fault_address));
	printk("pde_relative=%d\n",PDE(fault_address));
	}

	node * a_node;
	if(!CHECK_NODE(NODE_BIT(PDE(fault_address)),bucket.nodes_tracking)) {
		a_node=kzalloc(SIZE_NODE,GFP_KERNEL);
		(a_node -> h).key_zone = ZONE2M(fault_address);
		(a_node->h).next_node = bucket.nodes_list;
		(bucket.nodes_list) = (void *)a_node;
		set_bit(NODE_BIT(PDE(fault_address)),&(bucket.nodes_tracking));
		goto update_body;
	}
	else {
		a_node = (node *)(bucket.nodes_list);
		while((a_node->h).next_node!=NULL) {
			if((a_node->h).key_zone == ZONE2M(fault_address))
				goto update_body;
			a_node = (node *) ((a_node->h).next_node);
		}
		if((a_node->h).key_zone==ZONE2M(fault_address))
			goto update_body;
	}
	
	update_body:
	update_body(pgd_index, a_node,PTE(fault_address));
	end_add_node: cmpxchg(&(bucket.spinlock),1,0); //unlocked
	#undef bucket
	return;	
}
	
//audit_structure
void audit_hashbucket(int code) {
	unsigned int i,j,k;
	for(i=0; i<NBUCKETS; i++) {
		bucket_data bd = buckets[i];
		if((bd.nodes_tracking)>0) {
//			printk("bucket data n° %d=%u\n",i,bd.nodes_tracking);
			for(k=0;k<N_NODES_PER_BUCKET;k++) {
				unsigned int ctb = CHECK_NODE(k,bd.nodes_tracking);
		//		if(ctb)
//					printk("bit %d = %d\n",k,ctb);
			}
			node * a_node = (node*)bd.nodes_list;
			//print_body(a_node);
			print_line(code , a_node);
		}
	}
}

//cleaning hashbucket
void clean_hashbucket() {
	unsigned int i;
	bucket_data * bd;

	for(i=0;i<SIBLING_PGD;i++){
		PIDs[i] = -1;
	}

	for(i=0; i<NBUCKETS; i++) {
		bd= &(buckets[i]);
		bd->nodes_tracking = 0;
		void * list = bd->nodes_list;
		void * list_aux = NULL;
		bd->nodes_list=NULL;
		//printk("list=%p\n",list);
		while(list!=NULL) {
			list_aux = (((node *)list)->h).next_node;
			clean_body(((node*)list));
			kfree(list);
			list=list_aux;
		}
	}
}

//printing structs' info
void printing_structs_info() {
	printk("SIZE_BUCKET_DATA=%d\n",SIZE_BUCKET_DATA);
	printk("SIZE_HEADER=%d\n",SIZE_HEADER);
	printk("SIZE_BODY=%d\n",SIZE_BODY);
	printk("SIZE_NODE=%d\n",SIZE_NODE);
	printk("NGROUPS=%d\n",NGROUPS);
	printing_structs_info_aux();
}

